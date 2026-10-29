# crequests

一个用 C11 实现的、API 风格接近 Python `requests` 的 HTTP/HTTPS 客户端库。面向 Windows，底层直接使用 Winsock 和 Schannel，**不依赖 libcurl、OpenSSL 或其他第三方运行时**。

> C++17 版本见 [`cpp/README.md`](cpp/README.md)：`cpprequests` 复用本项目的 C 核心，提供 RAII/STL API 和类型安全 JSON。

```c
cr_response *r = cr_get(NULL, "https://example.com/");
printf("%d\n%s\n", cr_response_status(r), cr_response_text(r));
cr_response_free(r);
```

## 特性

- `cr_session`：默认请求头、超时、Cookie jar、重定向策略
- `cr_request`：方法、URL、查询参数、请求头、JSON/表单/原始 body、multipart 文件上传
- Basic Auth、Bearer Token、URL 中的 `user:password`
- 自动跟随 301/302/303/307/308；支持相对 `Location`；302/303 的 POST→GET 规则；跨域时移除 `Authorization`
- 响应解析：状态码、原因短语、重复响应头、chunked、Content-Length、连接关闭定界、`100 Continue`
- Cookie：持久化到 session、Domain/Path/Secure/HttpOnly/Max-Age/Expires
- 连接超时、读取超时、响应体大小上限
- HTTPS 使用 Windows Schannel，默认校验证书链和主机名
- 静态库、DLL 两种构建方式
- 本地模拟服务器 + 186 项自动检查
- 中英文 API 注释，MIT 许可证

## 平台与依赖

- Windows 7 及以上，推荐 Windows 10/11（TLS 1.2/1.3 支持取决于系统）
- MinGW-w64 GCC（已在 GCC 16.2 / x86_64 上验证）或 MSVC 2019+
- C11
- 运行时只依赖系统 DLL：`WS2_32.dll`、`Secur32.dll`、`CRYPT32.dll`、`KERNEL32.dll`、`msvcrt.dll`
- 测试需要 Python 3.7+（只用标准库）

> 目前没有 POSIX/Linux 后端。`src/transport.c` 是 Windows 专用传输层。

## 快速开始

### 1. 静态库

```powershell
make
```

产物：

- `libcrequests.a`
- `build/get.exe`、`build/post_json.exe`、`build/session.exe`

也可以使用 CMake：

```powershell
cmake -S . -B build-cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake -j
ctest --test-dir build-cmake --output-on-failure
```

### 2. DLL

```powershell
make dll
```

产物：

- `libcrequests.dll`：x64 DLL
- `libcrequests.dll.a`：MinGW 导入库（不是静态库）
- `libcrequests.a`：静态库（如果之前执行过 `make`）

验证 DLL 导入和导出的消费者程序：

```powershell
make dll-test
```

CMake 构建 DLL：

```powershell
cmake -S . -B build-dll -G "MinGW Makefiles" -DBUILD_SHARED_LIBS=ON
cmake --build build-dll -j
```

MSVC：

```powershell
cmake -S . -B build-msvc -A x64 -DBUILD_SHARED_LIBS=ON
cmake --build build-msvc --config Release
```

DLL 导出 54 个 `cr_*` 公共 API；内部函数不会被导出（MinGW 下通过 `--exclude-all-symbols` + `__declspec(dllexport)` 控制）。

#### 使用 DLL

`include/crequests.h` 通过 `CREQUESTS_SHARED` 自动切换 `dllimport`：

```c
#define CREQUESTS_SHARED
#include "crequests.h"
```

MinGW 编译：

```powershell
gcc app.c -Iinclude -DCREQUESTS_SHARED libcrequests.dll.a `
    -lws2_32 -lsecur32 -lcrypt32 -o app.exe
copy libcrequests.dll .
.\app.exe
```

MSVC 编译（先构建出 `crequests.lib`）：

```bat
cl /Iinclude /DCREQUESTS_SHARED app.c crequests.lib ws2_32.lib secur32.lib crypt32.lib
```

如果通过 CMake 的 `target_link_libraries(app PRIVATE crequests)` 使用 DLL 目标，`CREQUESTS_SHARED` 会自动传递，不需要手写宏。

部署时必须把 `libcrequests.dll`（MSVC 下为 `crequests.dll`）放在：

- 可执行文件同目录，或
- 系统 DLL 搜索路径中的目录

**ABI 注意**：对象必须由库函数释放，不要把 `cr_response`、`cr_session` 等交给 `free()`；请使用对应的 `cr_*_free`。DLL 和调用方应使用相同架构（x64/x86）。

## 代码示例

### GET + 查询参数 + 自定义头

```c
#include <stdio.h>
#include "crequests.h"

int main(void)
{
    cr_session *session = cr_session_new();
    cr_request *request = cr_request_new("GET", "http://example.com/search");

    cr_request_add_query(request, "q", "hello world");
    cr_request_set_header(request, "Accept", "application/json");
    cr_request_set_timeout(request, 10000, 30000);

    cr_response *response = cr_session_send(session, request);
    if (response == NULL) {
        fprintf(stderr, "out of memory\n");
    } else if (cr_response_error(response) != CR_OK) {
        fprintf(stderr, "%s\n", cr_response_error_message(response));
    } else {
        printf("%d %s\n", cr_response_status(response), cr_response_reason(response));
        printf("%s\n", cr_response_text(response));
    }

    cr_response_free(response);
    cr_request_free(request);
    cr_session_free(session);
    return 0;
}
```

### POST JSON

```c
cr_request *req = cr_request_new("POST", "https://httpbin.org/post");
cr_request_set_json(req, "{\"name\":\"crequests\",\"lang\":\"C\"}");
cr_response *res = cr_session_send(session, req);
```

`cr_request_set_json` 会原样发送字符串，并设置 `Content-Type: application/json`，不做 JSON 序列化或校验。

### 表单 / 文件上传

```c
cr_request *req = cr_request_new("POST", "http://example.com/upload");
cr_request_add_form(req, "name", "crequests");
cr_request_add_file(req, "avatar", "avatar.png", "image/png");
```

文件在发送时读入内存，以 `multipart/form-data` 发送；`content_type` 传 `NULL` 时按扩展名推断。

### Session 与 Cookie

```c
cr_session *session = cr_session_new();
cr_session_set_header(session, "User-Agent", "my-app/1.0");
cr_session_set_timeout(session, 5000, 20000);

/* 服务器 Set-Cookie 后，后续请求自动携带 */
cr_response *r1 = cr_get(session, "http://example.com/login");
cr_response *r2 = cr_get(session, "http://example.com/profile");

printf("cookies: %zu\n", cr_session_cookie_count(session));
```

### 认证

```c
cr_request_set_basic_auth(req, "alice", "secret");
/* 或 */
cr_request_set_bearer_token(req, "eyJhbGciOi...");
```

也可以直接使用 `http://user:pass@host/`，库会转换成 Basic Auth；URL 原始字符串和 `cr_response_url()` 中不会暴露密码。

## API 概览

完整声明和注释见 [`include/crequests.h`](include/crequests.h)。

### 会话

| 函数 | 说明 |
| --- | --- |
| `cr_session_new()` / `cr_session_free()` | 创建/销毁会话 |
| `cr_session_set_header(s, name, value)` | 设置默认头；`value == NULL` 删除 |
| `cr_session_set_timeout(s, connect_ms, read_ms)` | 毫秒；0 表示无限等待 |
| `cr_session_set_verify(s, 0/1)` | 是否校验 TLS 证书，默认 1 |
| `cr_session_set_follow_redirects(s, 0/1)` | 是否自动跟随重定向 |
| `cr_session_set_max_redirects(s, n)` | 最大重定向次数，默认 20 |
| `cr_session_set_max_body_size(s, bytes)` | 响应体上限，0 为不限制；默认 64 MiB |
| `cr_session_set_user_agent(s, ua)` | 默认 User-Agent |
| `cr_session_clear_cookies()` / `cr_session_cookie_count()` | Cookie jar 操作 |

### 请求

| 函数 | 说明 |
| --- | --- |
| `cr_request_new(method, url)` | 创建请求；非法 URL 返回 `NULL` |
| `cr_request_set_method/url` | 修改方法/URL |
| `cr_request_set_header` | 覆盖同名会话头；`NULL` 删除 |
| `cr_request_add_query(name, value)` | 添加查询参数，自动百分号编码 |
| `cr_request_add_form(name, value)` | 添加表单字段 |
| `cr_request_add_file(field, path, type)` | 添加 multipart 文件 |
| `cr_request_set_body(data, len)` | 原始字节 body |
| `cr_request_set_body_text(text, type)` | 文本 body |
| `cr_request_set_json(json)` | JSON 字符串 body |
| `cr_request_set_basic_auth` / `set_bearer_token` | 认证 |
| `cr_request_set_timeout(connect, read)` | 每次请求超时；`-1` 继承会话 |
| `cr_request_set_verify` / `set_follow_redirects` | `-1` 继承会话 |
| `cr_request_set_max_redirects` | `-1` 继承会话 |
| `cr_request_set_user_agent` | 每次请求 User-Agent |

### 发送与便捷函数

| 函数 | 说明 |
| --- | --- |
| `cr_session_send(session, request)` | 发送请求，始终尽量返回 response |
| `cr_get(s, url)` / `cr_head` / `cr_delete` | 便捷请求；`s` 可为 `NULL` |
| `cr_post(s, url, body, len, type)` | POST 原始 body |
| `cr_post_json(s, url, json)` | POST JSON |
| `cr_post_form(s, url, names, values, count)` | POST 表单 |

便捷函数传入 `session == NULL` 时会创建临时会话，因此 Cookie 不会跨调用保留。

### 响应

| 函数 | 说明 |
| --- | --- |
| `cr_response_error()` | `CR_OK` 或错误码 |
| `cr_response_error_message()` | 错误说明 |
| `cr_response_status()` / `cr_response_reason()` | 状态码/原因短语 |
| `cr_response_ok()` | 无传输错误且状态码 2xx/3xx |
| `cr_response_body(res, &len)` / `cr_response_text()` | body 指针/文本 |
| `cr_response_header(res, name)` | 第一个同名响应头 |
| `cr_response_header_count/name/value` | 遍历重复响应头 |
| `cr_response_url()` | 最终 URL |
| `cr_response_redirect_count()` | 重定向次数 |
| `cr_response_elapsed_ms()` | 总耗时（毫秒） |
| `cr_response_http_version()` | 10 / 11 / 0 |

响应体总会在末尾补一个 `'\0'`，但长度应以 `cr_response_body()` 返回的 length 为准，因为可能包含二进制数据。

## 错误处理

网络错误不会让 `cr_session_send()` 返回 `NULL`，而是返回一个带错误信息的 response：

```c
cr_response *r = cr_session_send(session, request);
if (r == NULL) {
    /* 内存耗尽 */
} else if (cr_response_error(r) != CR_OK) {
    fprintf(stderr, "%d: %s\n", cr_response_error(r),
            cr_response_error_message(r));
}
```

错误码：

| 错误码 | 含义 |
| --- | --- |
| `CR_OK` | 成功 |
| `CR_ERR_ARGUMENT` | 参数非法 |
| `CR_ERR_NOMEM` | 内存不足 |
| `CR_ERR_URL` | URL 非法 |
| `CR_ERR_UNSUPPORTED` | 不支持的协议/编码 |
| `CR_ERR_TLS` | TLS 握手/加解密错误 |
| `CR_ERR_TLS_VERIFY` | 证书校验失败 |
| `CR_ERR_DNS` | DNS 解析失败 |
| `CR_ERR_CONNECT` | TCP 连接失败 |
| `CR_ERR_TIMEOUT` | 超时 |
| `CR_ERR_SEND` | 发送失败 |
| `CR_ERR_RECV` | 接收失败 |
| `CR_ERR_PROTOCOL` | HTTP 协议错误 |
| `CR_ERR_TOO_MANY_REDIRECTS` | 重定向过多 |
| `CR_ERR_BODY_TOO_LARGE` | 响应体超过上限 |
| `CR_ERR_FILE` | 上传文件读取失败 |
| `CR_ERR_CLOSED` | 连接关闭 |

## 行为细节

- 默认 `User-Agent: crequests/1.0.0`，`Accept: */*`，`Accept-Encoding: identity`，`Connection: close`
- 重定向：
  - 301/302 的 POST 会变成 GET 并丢弃 body
  - 303 的非 HEAD 请求会变成 GET
  - 307/308 保留方法和 body
  - 跨 scheme/host/port 时移除 `Authorization`
  - 每次重定向都会重新计算 Cookie
- 查询参数只应用于初始 URL；重定向后的 `Location` 查询参数以服务器返回为准
- Cookie 使用简化 RFC 6265 实现：Domain 匹配、Path 匹配、Secure、过期时间；不实现 Public Suffix List
- 响应头大小上限 1 MiB，单行上限 64 KiB，数量上限 512
- Cookie jar 最多 256 条
- TLS 证书使用 Windows 证书存储验证；`cr_session_set_verify(0)` 仅建议用于本地测试
- 会话不是线程安全的；每个线程使用独立会话，或由调用方加锁

## 测试

```powershell
make test
```

测试会用 Python 启动一个本地 HTTP 服务器，覆盖：

- GET/HEAD/POST、查询参数、表单、JSON、multipart
- Basic/Bearer 认证
- 重定向链、相对 Location、POST→GET、307 保 body、跨域移除认证
- Cookie 的 Path/Secure/Max-Age、重复 Set-Cookie、大小写敏感名称
- chunked、204、404、418、100 Continue、重复响应头、UTF-8
- 超时、响应体上限、Content-Length 不完整
- URL/参数/头注入等参数校验
- DLL 导入和 HTTP 请求（`make dll-test`）

当前测试结果：

```text
186 checks, 0 failures
```

### 关于本机 HTTPS 测试

本机沙箱环境中 Windows 安全包无法取得 Schannel 凭据（`AcquireCredentialsHandle` 返回 `SEC_E_NO_CREDENTIALS`）；系统 `curl.exe`、PowerShell `Invoke-WebRequest`、WinHTTP 在这里也会同样失败。因此自动测试目前只覆盖 HTTP，HTTPS 的 Schannel 代码路径需要在正常桌面会话中验证。证书校验默认开启。

## 项目结构

```text
C_request/
├── include/crequests.h       公共 API
├── src/
│   ├── client.c              会话/请求/响应、重定向
│   ├── http.c                HTTP/1.1 请求与响应解析
│   ├── transport.c           Winsock + Schannel
│   ├── url.c                 URL 解析/编码/相对路径
│   ├── cookies.c             Cookie jar
│   ├── util.c                buffer/header/编码/时间
│   └── internal.h
├── examples/                 get / post_json / session
├── tests/
│   ├── test_crequests.c
│   ├── mock_server.py
│   ├── run_tests.py
│   └── dll_smoke.c
├── CMakeLists.txt
├── Makefile
└── README.md
```

## 限制

- 仅 HTTP/1.1，不支持 HTTP/2
- 不自动解压 gzip/br；默认请求 identity，若服务器仍压缩则返回原始字节
- 不支持代理、客户端证书、Digest/NTLM 认证、HTTP/2、异步请求
- 请求体和响应体都在内存中；大文件上传会整体读入
- 没有连接池（每个请求 `Connection: close`）
- DNS 解析本身没有超时控制
- Cookie 实现不包含 Public Suffix List

## License

MIT，见 [LICENSE](LICENSE)。
