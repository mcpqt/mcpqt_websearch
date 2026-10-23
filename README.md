# mcpqt_websearch

A DuckDuckGo web search plugin for [`mcpqt_host`](../mcpqt_host). It exposes
search, page fetching, and link expansion as MCP tools. No API key is required.

## Requirements

- C++20 and Qt 6.4+
- An installed `mcpqt_host` to load the built plugin

## Build

```sh
cmake -S . -B build
cmake --build build
```

This produces `build/mcpqt_websearch.so` (`.dylib` / `.dll` on other
platforms).

To use the vendored Lexbor HTML parser instead of the Qt XML fallback:

```sh
cmake -S . -B build -DDDG_USE_LEXBOR=ON
cmake --build build
```

## Run

Load the module directly or copy it into the host's plugin directory:

```sh
# stdio
/path/to/mcp_stdio_server ./build/mcpqt_websearch.so

# Streamable HTTP
/path/to/mcp_http_server --port 8848 ./build/mcpqt_websearch.so
```

Test with the bundled client:

```sh
/path/to/mcp_tester /path/to/mcp_stdio_server ./build/mcpqt_websearch.so
```

## Tools

| Tool | Description |
| --- | --- |
| `websearch/search` | Search DuckDuckGo and return titles, URLs, and snippets. |
| `websearch/fetch_content` | Fetch a page and extract its main text. Supports pagination. |
| `websearch/expand_link` | Resolve a `ref://<id>` token from search results to its full URL. |

Search results may include shortened `ref://` links. Fetching accepts either a
full URL or a reference token. Markdown extraction omits images and scripts.

## Configuration

Configuration is read from environment variables at plugin load:

| Variable | Default | Purpose |
| --- | --- | --- |
| `DDG_SAFE_SEARCH` | `MODERATE` | Safe-search level: `STRICT`, `MODERATE`, or `OFF`. |
| `DDG_REGION` | `wt-wt` | Default region/language code. |
| `DDG_PARSE_MODE` | `text` | Page extraction mode: `text`, `main`, or `markdown`. |
| `DDG_SEARCH_BACKEND`, `DDG_FETCH_BACKEND` | build default | Backend override. |
| `DDG_SEARCH_RPM`, `DDG_FETCH_RPM`, `DDG_FETCH_HOST_RPM` | `30`, `20`, `0` | Rate limits (requests per minute). |
| `DDG_CACHE_TTL`, `DDG_CACHE_MAX_ENTRIES` | `300`, `64` | Fetch cache lifetime and size. |
| `DDG_RATE_LIMIT_STRATEGY` | `sliding` | Rate-limiter strategy. |
| `DDG_SSL_VERIFY`, `DDG_CA_CERTS` | enabled | TLS verification and CA bundle path. |
| `DDG_ALLOW_PRIVATE_URLS` | disabled | Allow fetching private/local URLs. |
| `DDG_REF_URL_THRESHOLD` | build default | URL length threshold for shortened links. |

## License

GNU Affero General Public License v3.0 (`AGPL-3.0-only`). See `LICENSE`.
