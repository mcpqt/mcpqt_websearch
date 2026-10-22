// SPDX-License-Identifier: AGPL-3.0-only
//
// DuckDuckGo web search plugin. Wraps the ddg core searcher/fetcher and exposes
// the three original MCP tools as public slots. Tool metadata lives in
// websearch_plugin.json.

#include "plugin.h"

#include "constants.hpp"
#include "context.hpp"
#include "fetcher.hpp"
#include "link_registry.hpp"
#include "safe_search_mode.hpp"
#include "searcher.hpp"
#include "tls_verify.hpp"
#include "utils.hpp"

#include <QObject>
#include <QString>
#include <QVariant>

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

namespace
{

  std::optional<std::string> env_str(const char* name)
  {
    const char* v = std::getenv(name);
    if(!v || !*v)
    {
      return std::nullopt;
    }
    return std::string(v);
  }

  bool env_flag(const char* name)
  {
    const auto v = env_str(name);
    if(!v)
    {
      return false;
    }
    const std::string s = ddg::to_lower(ddg::trim(*v));
    return s == "1" || s == "true" || s == "yes" || s == "on";
  }

  std::optional<int> env_int(const char* name, int minimum)
  {
    const auto v = env_str(name);
    if(!v)
    {
      return std::nullopt;
    }
    try
    {
      const int value = std::stoi(ddg::trim(*v));
      if(value < minimum)
      {
        return std::nullopt;
      }
      return value;
    }
    catch(...)
    {
      return std::nullopt;
    }
  }

  class WebsearchPlugin : public mcp::PluginInterface
  {
    Q_OBJECT
    Q_CLASSINFO("id", "websearch")
    Q_CLASSINFO("version", "0.1.0")
    Q_CLASSINFO(
        "description",
        "DuckDuckGo web search and content fetching.")
    Q_CLASSINFO("license", "AGPL-3.0-only")
    Q_PLUGIN_METADATA(IID MCP_PLUGIN_IID FILE "websearch_plugin.json")
    Q_INTERFACES(mcp::PluginInterface)

  public:
    explicit WebsearchPlugin(QObject* parent = nullptr)
        : mcp::PluginInterface(parent)
    {
    }

    void initialize(const mcp::HostInterface& host) override
    {
      m_host = host;
      m_context = ddg::Context(
          [this](int level, const std::string& message)
          {
            if(m_host.log)
            {
              m_host.log(level, QString::fromStdString(message));
            }
          });

      // ---- configuration from DDG_* environment variables ----------------
      std::string safe_search_name = "MODERATE";
      if(const auto v = env_str("DDG_SAFE_SEARCH"))
      {
        safe_search_name = ddg::to_lower(ddg::trim(*v));
      }

      std::string region = "wt-wt";
      if(const auto v = env_str("DDG_REGION"))
      {
        region = ddg::trim(*v);
      }

      std::string search_backend = ddg::kCompiledFetchBackend;
      if(const auto v = env_str("DDG_SEARCH_BACKEND"))
      {
        search_backend = ddg::to_lower(ddg::trim(*v));
      }

      std::string fetch_backend = ddg::kCompiledFetchBackend;
      if(const auto v = env_str("DDG_FETCH_BACKEND"))
      {
        fetch_backend = ddg::to_lower(ddg::trim(*v));
      }

      ddg::TlsVerify tls;
      if(const auto v = env_str("DDG_SSL_VERIFY"))
      {
        const std::string s = ddg::to_lower(ddg::trim(*v));
        tls.enabled = !(s == "0" || s == "false" || s == "no" || s == "off");
      }
      if(const auto v = env_str("DDG_CA_CERTS"))
      {
        tls.ca_bundle = ddg::trim(*v);
      }

      const int search_rpm = env_int("DDG_SEARCH_RPM", 1).value_or(30);
      const int fetch_rpm = env_int("DDG_FETCH_RPM", 1).value_or(20);
      const int fetch_host_rpm = env_int("DDG_FETCH_HOST_RPM", 0).value_or(0);
      const int cache_ttl = env_int("DDG_CACHE_TTL", 0).value_or(300);
      const int cache_max_entries
          = env_int("DDG_CACHE_MAX_ENTRIES", 0).value_or(64);
      const int ref_threshold
          = env_int("DDG_REF_URL_THRESHOLD", 0).value_or(
              ddg::kDefaultRefUrlThreshold);

      std::string rate_strategy = "sliding";
      if(const auto v = env_str("DDG_RATE_LIMIT_STRATEGY"))
      {
        rate_strategy = ddg::to_lower(ddg::trim(*v));
      }

      std::string parse_mode = "text";
      if(const auto v = env_str("DDG_PARSE_MODE"))
      {
        parse_mode = ddg::to_lower(ddg::trim(*v));
      }

      const ddg::SafeSearchMode mode
          = ddg::safe_search_from_name(safe_search_name)
                .value_or(ddg::SafeSearchMode::Moderate);

      m_links = std::make_shared<ddg::LinkRegistry>();

      ddg::DuckDuckGoSearcher::Config search_cfg;
      search_cfg.safe_search = mode;
      search_cfg.default_region = region;
      search_cfg.backend = search_backend;
      search_cfg.ssl_verify = tls;
      search_cfg.requests_per_minute = search_rpm;
      search_cfg.rate_limit_strategy = rate_strategy;
      search_cfg.ref_url_threshold = ref_threshold;
      m_searcher
          = std::make_unique<ddg::DuckDuckGoSearcher>(search_cfg, m_links);

      ddg::WebContentFetcher::Config fetch_cfg;
      fetch_cfg.backend = fetch_backend;
      fetch_cfg.allow_private_urls = env_flag("DDG_ALLOW_PRIVATE_URLS");
      fetch_cfg.ssl_verify = tls;
      fetch_cfg.requests_per_minute = fetch_rpm;
      fetch_cfg.host_requests_per_minute = fetch_host_rpm;
      fetch_cfg.rate_limit_strategy = rate_strategy;
      fetch_cfg.cache_ttl = static_cast<double>(cache_ttl);
      fetch_cfg.cache_max_entries = cache_max_entries;
      fetch_cfg.parse_mode = parse_mode;
      m_fetcher
          = std::make_unique<ddg::WebContentFetcher>(fetch_cfg, m_links);

      registerPublicSlots();
    }

    void shutdown(void) override
    {
      m_fetcher.reset();
      m_searcher.reset();
      m_links.reset();
    }

  public slots:
    QString search(QString query, int max_results, QString region)
    {
      try
      {
        const auto results = m_searcher->search(
            query.toStdString(), m_context,
            max_results <= 0 ? 10 : max_results, region.toStdString());
        return QString::fromStdString(
            m_searcher->format_results_for_llm(results));
      }
      catch(const std::exception& e)
      {
        return QStringLiteral("An error occurred while searching: ")
               + QString::fromStdString(e.what());
      }
    }

    QString fetchContent(QString url, int start_index, int max_length,
                         QString backend, QString parse_mode)
    {
      try
      {
        const std::optional<std::string> backend_opt
            = backend.isEmpty()
                  ? std::nullopt
                  : std::optional<std::string>(backend.toStdString());
        const std::optional<std::string> parse_opt
            = parse_mode.isEmpty()
                  ? std::nullopt
                  : std::optional<std::string>(parse_mode.toStdString());
        return QString::fromStdString(m_fetcher->fetch_and_parse(
            url.toStdString(), m_context, start_index,
            max_length <= 0 ? 8000 : max_length, backend_opt, parse_opt));
      }
      catch(const std::exception& e)
      {
        return QStringLiteral("An error occurred while fetching: ")
               + QString::fromStdString(e.what());
      }
    }

    QString expandLink(QString token)
    {
      if(const auto url = m_links->resolve(token.toStdString()))
      {
        return QString::fromStdString(*url);
      }
      return QString::fromStdString(
          ddg::unknown_ref_error(token.toStdString()));
    }

  private:
    mcp::HostInterface m_host;
    ddg::Context m_context;
    std::shared_ptr<ddg::LinkRegistry> m_links;
    std::unique_ptr<ddg::DuckDuckGoSearcher> m_searcher;
    std::unique_ptr<ddg::WebContentFetcher> m_fetcher;
  };

} // namespace

#include "websearch_plugin.moc"
