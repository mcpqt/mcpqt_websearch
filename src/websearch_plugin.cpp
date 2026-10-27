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

#include <QByteArray>
#include <QFuture>
#include <QObject>
#include <QString>
#include <QVariant>

#include <exception>
#include <memory>
#include <optional>

namespace
{

  QString env_str(const char* name)
  {
    const QByteArray value = qgetenv(name);
    return value.isEmpty() ? QString() : QString::fromLocal8Bit(value);
  }

  bool env_flag(const char* name)
  {
    const QString v = env_str(name);
    if(v.isNull())
    {
      return false;
    }
    const QString s = ddg::to_lower(ddg::trim(v));
    return s == QStringLiteral("1") || s == QStringLiteral("true")
           || s == QStringLiteral("yes") || s == QStringLiteral("on");
  }

  std::optional<int> env_int(const char* name, int minimum)
  {
    const QString v = env_str(name);
    if(v.isNull())
    {
      return std::nullopt;
    }
    bool ok = false;
    const int value = v.trimmed().toInt(&ok);
    if(!ok || value < minimum)
    {
      return std::nullopt;
    }
    return value;
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
      QString safe_search_name = QStringLiteral("MODERATE");
      if(const QString v = env_str("DDG_SAFE_SEARCH"); !v.isNull())
      {
        safe_search_name = ddg::to_lower(ddg::trim(v));
      }

      QString region = QStringLiteral("wt-wt");
      if(const QString v = env_str("DDG_REGION"); !v.isNull())
      {
        region = ddg::trim(v);
      }

      QString search_backend = ddg::kCompiledFetchBackend;
      if(const QString v = env_str("DDG_SEARCH_BACKEND"); !v.isNull())
      {
        search_backend = ddg::to_lower(ddg::trim(v));
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

      QString rate_strategy = QStringLiteral("sliding");
      if(const QString v = env_str("DDG_RATE_LIMIT_STRATEGY"); !v.isNull())
      {
        rate_strategy = ddg::to_lower(ddg::trim(v));
      }

      QString parse_mode = QStringLiteral("markdown");
      if(const QString v = env_str("DDG_PARSE_MODE"); !v.isNull())
      {
        parse_mode = ddg::to_lower(ddg::trim(v));
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
        return QStringLiteral("An error occurred while searching: ")
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
        return QStringLiteral("An error occurred while fetching: ")
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
