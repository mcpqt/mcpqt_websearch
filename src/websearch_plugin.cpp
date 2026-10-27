// SPDX-License-Identifier: AGPL-3.0-only
//
// DuckDuckGo web search plugin. Wraps the ddg core searcher/fetcher and exposes
// the three original MCP tools as public slots. Tool metadata lives in
// websearch_plugin.json.

#include "plugin.h"

#include "constants.hpp"
#include "fetcher.hpp"
#include "link_registry.hpp"
#include "log.hpp"
#include "safe_search_mode.hpp"
#include "searcher.hpp"
#include "utils.hpp"
#include "webio_transport.hpp"

#include <QFuture>
#include <QObject>
#include <QString>
#include <QtGlobal>
#include <QVariant>

#include <exception>
#include <memory>

namespace
{

  bool env_flag(const char* name)
  {
    const QString v = qEnvironmentVariable(name);
    if(v.isEmpty())
    {
      return false;
    }
    const QString s = v.trimmed().toLower();
    return s == "1" || s == "true"
           || s == "yes" || s == "on";
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
      // webio's httpGet/httpPost return QFuture<QVariant>. The host marshals
      // that return value into a QVariant, so the template instantiation must
      // be registered in this process before callFunc invokes webio.
      qRegisterMetaType<QFuture<QVariant>>("QFuture<QVariant>");
      const ddg::Transport transport
          = ddg::make_webio_transport(m_host.callFunc);
      m_log = [this](int level, const QString& message)
      {
        if(m_host.log)
        {
          m_host.log(level, message);
        }
      };

      // ---- configuration from DDG_* environment variables ----------------
      QString safe_search_name = "MODERATE";
      if(const QString v = qEnvironmentVariable("DDG_SAFE_SEARCH");
         !v.isEmpty())
      {
        safe_search_name = v.trimmed().toLower();
      }

      QString region = "wt-wt";
      if(const QString v = qEnvironmentVariable("DDG_REGION"); !v.isEmpty())
      {
        region = v.trimmed();
      }

      QString search_backend = ddg::kCompiledFetchBackend;
      if(const QString v = qEnvironmentVariable("DDG_SEARCH_BACKEND");
         !v.isEmpty())
      {
        search_backend = v.trimmed().toLower();
      }

      const int search_rpm
          = qEnvironmentVariable("DDG_SEARCH_RPM", "30").toInt();
      const int fetch_rpm
          = qEnvironmentVariable("DDG_FETCH_RPM", "20").toInt();
      const int fetch_host_rpm
          = qEnvironmentVariable("DDG_FETCH_HOST_RPM", "0").toInt();
      const int cache_ttl
          = qEnvironmentVariable("DDG_CACHE_TTL", "300").toInt();
      const int cache_max_entries
          = qEnvironmentVariable("DDG_CACHE_MAX_ENTRIES", "64").toInt();
      const int ref_threshold
          = qEnvironmentVariable(
                "DDG_REF_URL_THRESHOLD",
                QString::number(ddg::kDefaultRefUrlThreshold))
                .toInt();

      QString rate_strategy = "sliding";
      if(const QString v = qEnvironmentVariable("DDG_RATE_LIMIT_STRATEGY");
         !v.isEmpty())
      {
        rate_strategy = v.trimmed().toLower();
      }

      QString parse_mode = "markdown";
      if(const QString v = qEnvironmentVariable("DDG_PARSE_MODE");
         !v.isEmpty())
      {
        parse_mode = v.trimmed().toLower();
      }

      const ddg::SafeSearchMode mode
          = ddg::safe_search_from_name(safe_search_name)
                .value_or(ddg::SafeSearchMode::Moderate);

      m_links = std::make_shared<ddg::LinkRegistry>();

      ddg::DuckDuckGoSearcher::Config search_cfg;
      search_cfg.safe_search = mode;
      search_cfg.default_region = region;
      search_cfg.backend = search_backend;
      search_cfg.transport = transport;
      search_cfg.requests_per_minute = search_rpm;
      search_cfg.rate_limit_strategy = rate_strategy;
      search_cfg.ref_url_threshold = ref_threshold;
      m_searcher
          = std::make_unique<ddg::DuckDuckGoSearcher>(search_cfg, m_links);

      ddg::WebContentFetcher::Config fetch_cfg;
      fetch_cfg.allow_private_urls = env_flag("DDG_ALLOW_PRIVATE_URLS");
      fetch_cfg.transport = transport;
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
    QString search(QString query, int max_results, QString region, QString timelimit)
    {
      try
      {
        const auto results = m_searcher->search(
            query, m_log, max_results <= 0 ? 10 : max_results, region, timelimit);
        return m_searcher->format_results_for_llm(results);
      }
      catch(const std::exception& e)
      {
        return "An error occurred while searching: "
               + QString::fromUtf8(e.what());
      }
    }

    QString fetchContent(QString url, int start_index, int max_length,
                         QString parse_mode)
    {
      try
      {
        const QString parse_arg = parse_mode.isEmpty() ? QString() : parse_mode;
        return m_fetcher->fetch_and_parse(
            url, m_log, start_index, max_length <= 0 ? 8000 : max_length,
            parse_arg);
      }
      catch(const std::exception& e)
      {
        return "An error occurred while fetching: "
               + QString::fromUtf8(e.what());
      }
    }

    QString expandLink(QString token)
    {
      const QString url = m_links->resolve(token);
      if(!url.isNull())
      {
        return url;
      }
      return ddg::unknown_ref_error(token);
    }

  private:
    ddg::LogFn m_log;
    std::shared_ptr<ddg::LinkRegistry> m_links;
    std::unique_ptr<ddg::DuckDuckGoSearcher> m_searcher;
    std::unique_ptr<ddg::WebContentFetcher> m_fetcher;
  };

} // namespace

#include "websearch_plugin.moc"
