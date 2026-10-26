# mcpqt_websearch

A DuckDuckGo web search plugin for [`mcpqt_host`](../mcpqt_host). It exposes
search, page fetching, and link expansion as MCP tools. No API key is required.

## Requirements

- C++20 and Qt 6.4+
- The [`mcpqt_webio`](../mcpqt_webio) plugin, loaded before websearch
- An installed `mcpqt_host` to load the built plugins

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

websearch delegates HTTP access to the `webio` plugin. Load `webio` first, or
place both modules in a directory passed to the host:

```sh
# stdio
/path/to/mcp_stdio_server ./build/mcpqt_webio.so ./build/mcpqt_websearch.so

# Streamable HTTP
/path/to/mcp_http_server --port 8848 ./build/mcpqt_webio.so ./build/mcpqt_websearch.so
```

The host retries dependencies, so a directory containing both modules also
works. Build `webio` from the sibling `../mcpqt_webio` project.

Test with the bundled client:

```sh
/path/to/mcp_tester /path/to/mcp_stdio_server ./build/mcpqt_webio.so ./build/mcpqt_websearch.so
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
| `DDG_SEARCH_BACKEND`, `DDG_FETCH_BACKEND` | build default | Advisory backend name (webio always performs the request). |
| `DDG_SEARCH_RPM`, `DDG_FETCH_RPM`, `DDG_FETCH_HOST_RPM` | `30`, `20`, `0` | Rate limits (requests per minute). |
| `DDG_CACHE_TTL`, `DDG_CACHE_MAX_ENTRIES` | `300`, `64` | Fetch cache lifetime and size. |
| `DDG_RATE_LIMIT_STRATEGY` | `sliding` | Rate-limiter strategy. |
| `DDG_ALLOW_PRIVATE_URLS` | disabled | Allow fetching private/local URLs. |
| `DDG_REF_URL_THRESHOLD` | build default | URL length threshold for shortened links. |

TLS verification and the CA bundle are configured on the `webio` plugin with
`WEBIO_SSL_VERIFY` and `WEBIO_CA_CERTS`. websearch no longer performs network
I/O itself; the `DDG_*` rate-limit, cache, parse, and SSRF settings above still
apply.

## License

GNU Affero General Public License v3.0 (`AGPL-3.0-only`). See `LICENSE`.
