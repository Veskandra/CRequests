# crequests / cpprequests API 参考

- 版本：1.0.0
- C 头文件：`include/crequests.h`
- C++ 头文件：`cpp/include/cpprequests.hpp`、`cpp/include/cpprequests/json.hpp`
- 平台：Windows 7+（推荐 Windows 10/11），x86/x64
- 运行时：Winsock + Schannel，无 libcurl/OpenSSL 等第三方依赖
- 许可证：MIT

本文档同时覆盖 C API 和 C++17 API。C++ 封装复用 C 核心，网络、Cookie、重定向、TLS 行为与 C 版一致。

---

## 目录

- [1. 文档约定](#1-文档约定)
- [2. 编译与链接](#2-编译与链接)
- [3. C API](#3-c-api)
  - [3.1 对象模型](#31-对象模型)
  - [3.2 错误处理](#32-错误处理)
  - [3.3 Session API](#33-session-api)
  - [3.4 Request API](#34-request-api)
  - [3.5 发送请求与便捷函数](#35-发送请求与便捷函数)
  - [3.6 Response API](#36-response-api)
  - [3.7 选项继承与默认值](#37-选项继承与默认值)
  - [3.8 重定向](#38-重定向)
  - [3.9 Cookie](#39-cookie)
  - [3.10 请求编码与 wire 行为](#310-请求编码与-wire-行为)
  - [3.11 C 示例](#311-c-示例)
- [4. C++ API](#4-c-api)
  - [4.1 异常](#41-异常)
  - [4.2 Headers 与 File](#42-headers-与-file)
  - [4.3 Response](#43-response)
  - [4.4 Request](#44-request)
  - [4.5 Session](#45-session)
  - [4.6 自由函数](#46-自由函数)
  - [4.7 JSON](#47-json)
  - [4.8 C++ 示例](#48-c-示例)
- [5. 线程、所有权与 ABI](#5-线程所有权与-abi)
- [6. 限制](#6-限制)

---

## 1. 文档约定

- `cr_error` 的 `CR_OK` 值为 `0`，其余为顺序递增的错误码。
- 所有 `const char *` 参数都是 NUL 结尾的字符串；URL、请求头、Cookie 使用 UTF-8/ASCII。
- 所有 `size_t` 长度都是字节数，不是字符数。
- “必须”表示违反后函数返回错误或请求失败；“可选”表示允许传 `NULL` 或使用默认值。
- 除非特别说明，返回的指针都是**借用指针**，有效期到对应对象被释放或下一次修改为止。
- C API 中“拥有”一个对象的意思是：必须调用对应的 `cr_*_free`，不能交给 `free()`。
- 线程安全规则见[第 5 节](#5-线程所有权与-abi)。

---

## 2. 编译与链接

### 2.1 C 静态库

```powershell
gcc app.c -Iinclude libcrequests.a -lws2_32 -lsecur32 -lcrypt32 -o app.exe
```

### 2.2 C DLL

构建产物：

- `libcrequests.dll`
- `libcrequests.dll.a`（MinGW 导入库）
- MSVC 下为 `crequests.lib`

调用方必须定义 `CREQUESTS_SHARED`：

```c
#define CREQUESTS_SHARED
#include "crequests.h"
```

```powershell
gcc app.c -Iinclude -DCREQUESTS_SHARED libcrequests.dll.a `
    -lws2_32 -lsecur32 -lcrypt32 -o app.exe
copy libcrequests.dll .
```

部署时 `libcrequests.dll` 必须位于 exe 同目录或 DLL 搜索路径中。

### 2.3 C++ 库

```powershell
g++ -std=c++17 app.cpp -Icpp/include cpp/libcpprequests.a `
    -lws2_32 -lsecur32 -lcrypt32 -o app.exe
```

C++ DLL 使用 `CPPR_SHARED`：

```cpp
#define CPPR_SHARED
#include "cpprequests.hpp"
```

MinGW 动态链接时通常还需要部署：

- `libcpprequests.dll`
- `libgcc_s_seh-1.dll`
- `libstdc++-6.dll`

如果使用 CMake 的 `cpprequests` 目标，`CPPR_SHARED` 会自动传递。

### 2.4 CMake

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

DLL：

```powershell
cmake -S . -B build-dll -G "MinGW Makefiles" -DBUILD_SHARED_LIBS=ON
cmake --build build-dll -j
```

---

## 3. C API

### 3.1 对象模型

C API 有三个不透明对象：

| 类型 | 作用 | 生命周期 |
| --- | --- | --- |
| `cr_session` | 默认头、Cookie jar、默认超时和重定向策略 | `cr_session_new` → `cr_session_free` |
| `cr_request` | 方法、URL、查询参数、请求头、body、认证 | `cr_request_new` → `cr_request_free` |
| `cr_response` | 状态码、响应头、body、错误、耗时 | 由 `cr_session_send`/便捷函数返回，`cr_response_free` |

```c
cr_session *session = cr_session_new();
cr_request *request = cr_request_new("GET", "https://example.com/");
cr_response *response = cr_session_send(session, request);
/* ... 使用 response ... */
cr_response_free(response);
cr_request_free(request);
cr_session_free(session);
```

`cr_request` 不会被 `cr_session_send` 消耗，可以修改后再次发送。`cr_response` 不依赖 session/request，可以独立保存到它们被释放之后。

C API 不使用全局初始化函数；第一次网络操作会自动执行 `WSAStartup`。

### 3.2 错误处理

```c
typedef enum cr_error {
    CR_OK = 0,
    CR_ERR_ARGUMENT,
    CR_ERR_NOMEM,
    CR_ERR_URL,
    CR_ERR_UNSUPPORTED,
    CR_ERR_TLS,
    CR_ERR_TLS_VERIFY,
    CR_ERR_DNS,
    CR_ERR_CONNECT,
    CR_ERR_TIMEOUT,
    CR_ERR_SEND,
    CR_ERR_RECV,
    CR_ERR_PROTOCOL,
    CR_ERR_TOO_MANY_REDIRECTS,
    CR_ERR_BODY_TOO_LARGE,
    CR_ERR_FILE,
    CR_ERR_CLOSED
} cr_error;
```

| 错误码 | 值 | 含义 |
| --- | ---: | --- |
| `CR_OK` | 0 | 成功 |
| `CR_ERR_ARGUMENT` | 1 | 参数非法 |
| `CR_ERR_NOMEM` | 2 | 内存不足 |
| `CR_ERR_URL` | 3 | URL 格式错误 |
| `CR_ERR_UNSUPPORTED` | 4 | 不支持的协议或传输编码 |
| `CR_ERR_TLS` | 5 | TLS 握手、加密或解密失败 |
| `CR_ERR_TLS_VERIFY` | 6 | 证书链或主机名校验失败 |
| `CR_ERR_DNS` | 7 | DNS 解析失败 |
| `CR_ERR_CONNECT` | 8 | TCP 连接失败 |
| `CR_ERR_TIMEOUT` | 9 | 连接/读取超时 |
| `CR_ERR_SEND` | 10 | 发送失败 |
| `CR_ERR_RECV` | 11 | 接收失败 |
| `CR_ERR_PROTOCOL` | 12 | HTTP 协议错误 |
| `CR_ERR_TOO_MANY_REDIRECTS` | 13 | 超过最大重定向次数 |
| `CR_ERR_BODY_TOO_LARGE` | 14 | 响应体超过上限 |
| `CR_ERR_FILE` | 15 | 上传文件读取失败 |
| `CR_ERR_CLOSED` | 16 | 连接被关闭 |

```c
const char *cr_strerror(cr_error error);
const char *cr_version(void);
```

- `cr_strerror` 永远返回非 NULL 的英文名称。
- `cr_version` 返回 `"1.0.0"`。
- 版本宏：`CR_VERSION_MAJOR`、`CR_VERSION_MINOR`、`CR_VERSION_PATCH`、`CR_VERSION_STRING`。

错误分成两类：

1. **API 调用错误**：setter/编码函数直接返回 `cr_error`。
2. **传输/协议错误**：`cr_session_send` 仍返回 response，通过 `cr_response_error()` 和 `cr_response_error_message()` 查看。

```c
cr_response *r = cr_get(NULL, "https://example.com/");
if (r == NULL) {
    /* 内存分配失败 */
} else if (cr_response_error(r) != CR_OK) {
    fprintf(stderr, "%d: %s\n", cr_response_error(r),
            cr_response_error_message(r));
}
```

### 3.3 Session API

```c
cr_session *cr_session_new(void);
void cr_session_free(cr_session *session);

cr_error cr_session_set_header(cr_session *session,
                               const char *name,
                               const char *value);
cr_error cr_session_set_timeout(cr_session *session,
                                int connect_timeout_ms,
                                int read_timeout_ms);
cr_error cr_session_set_verify(cr_session *session, int verify);
cr_error cr_session_set_follow_redirects(cr_session *session, int follow);
cr_error cr_session_set_max_redirects(cr_session *session, int max_redirects);
cr_error cr_session_set_user_agent(cr_session *session,
                                   const char *user_agent);
cr_error cr_session_set_max_body_size(cr_session *session, size_t max_bytes);

void cr_session_clear_cookies(cr_session *session);
size_t cr_session_cookie_count(const cr_session *session);
```

| 函数 | 说明 |
| --- | --- |
| `cr_session_new` | 创建会话；失败返回 `NULL` |
| `cr_session_free` | 释放会话；允许 `NULL` |
| `cr_session_set_header` | 设置/替换默认请求头；`value == NULL` 删除 |
| `cr_session_set_timeout` | 默认连接/读取超时，毫秒；`0` 表示无限等待 |
| `cr_session_set_verify` | `1` 校验 TLS 证书（默认），`0` 关闭 |
| `cr_session_set_follow_redirects` | `1` 自动跟随重定向（默认），`0` 不跟随 |
| `cr_session_set_max_redirects` | 最大跟随次数，默认 `20`，必须 `>= 0` |
| `cr_session_set_user_agent` | 设置默认 User-Agent；`NULL` 恢复内置值 |
| `cr_session_set_max_body_size` | 响应体上限，字节；`0` 表示不限制，默认 64 MiB |
| `cr_session_clear_cookies` | 清空 Cookie jar |
| `cr_session_cookie_count` | 当前 jar 中的 Cookie 数量 |

默认值：

| 选项 | 默认值 |
| --- | --- |
| 连接超时 | 10000 ms |
| 读取超时 | 30000 ms |
| TLS 校验 | 开启 |
| 跟随重定向 | 开启 |
| 最大重定向次数 | 20 |
| 响应体上限 | 64 MiB |
| User-Agent | `crequests/1.0.0` |

说明：

- 请求头名称必须是 HTTP token，不能含 `:`、CR、LF；值不能含 CR/LF。
- 超时是针对单次连接/单次读取操作的超时，不是整个请求的总时限。重定向会建立新连接，因此总耗时可能超过单个读超时。
- 关闭 TLS 校验只建议用于本地测试。
- Cookie jar 最多保存 256 条 Cookie。

### 3.4 Request API

```c
cr_request *cr_request_new(const char *method, const char *url);
void cr_request_free(cr_request *request);

cr_error cr_request_set_method(cr_request *request, const char *method);
cr_error cr_request_set_url(cr_request *request, const char *url);
cr_error cr_request_set_header(cr_request *request,
                               const char *name,
                               const char *value);

cr_error cr_request_add_query(cr_request *request,
                              const char *name,
                              const char *value);
cr_error cr_request_add_form(cr_request *request,
                             const char *name,
                             const char *value);
cr_error cr_request_add_file(cr_request *request,
                             const char *field_name,
                             const char *path,
                             const char *content_type);

cr_error cr_request_set_body(cr_request *request,
                             const void *data,
                             size_t len);
cr_error cr_request_set_body_text(cr_request *request,
                                  const char *text,
                                  const char *content_type);
cr_error cr_request_set_json(cr_request *request, const char *json);

cr_error cr_request_set_basic_auth(cr_request *request,
                                   const char *username,
                                   const char *password);
cr_error cr_request_set_bearer_token(cr_request *request, const char *token);

cr_error cr_request_set_timeout(cr_request *request,
                                int connect_timeout_ms,
                                int read_timeout_ms);
cr_error cr_request_set_verify(cr_request *request, int verify);
cr_error cr_request_set_follow_redirects(cr_request *request, int follow);
cr_error cr_request_set_max_redirects(cr_request *request, int max_redirects);
cr_error cr_request_set_user_agent(cr_request *request, const char *user_agent);
```

| 函数 | 说明 |
| --- | --- |
| `cr_request_new` | 创建请求；method 不区分大小写并会转成大写；URL 必须是绝对 `http://`/`https://` URL；非法时返回 `NULL` |
| `cr_request_set_method` | 修改方法；必须是 HTTP token |
| `cr_request_set_url` | 修改 URL；重新校验 |
| `cr_request_set_header` | 覆盖同名 session 头；`value == NULL` 删除该请求头 |
| `cr_request_add_query` | 追加查询参数，自动编码；`value == NULL` 生成裸键 `?flag` |
| `cr_request_add_form` | 追加表单字段；切换到 form body 模式 |
| `cr_request_add_file` | 追加 multipart 文件；切换到 multipart 模式，保留已有表单字段 |
| `cr_request_set_body` | 设置原始二进制 body；会清空 form/file；`data == NULL, len == 0` 清空 body |
| `cr_request_set_body_text` | 设置文本 body；`content_type` 非 NULL 时设置 Content-Type |
| `cr_request_set_json` | 设置原始 JSON 字符串并设置 `Content-Type: application/json`；不校验 JSON |
| `cr_request_set_basic_auth` | Basic Auth；密码可为 `NULL`（按空字符串） |
| `cr_request_set_bearer_token` | Bearer Token；调用后清除 Basic Auth |
| `cr_request_set_timeout` | 请求级超时；`-1` 表示继承 session，`0` 表示无限等待 |
| `cr_request_set_verify` | `0/1` 显式覆盖；`-1` 继承 |
| `cr_request_set_follow_redirects` | `0/1` 显式覆盖；`-1` 继承 |
| `cr_request_set_max_redirects` | `>= 0` 显式覆盖；`-1` 继承 |
| `cr_request_set_user_agent` | 请求级 User-Agent；`NULL` 恢复为继承 session |

#### Body 模式

| 调用 | 结果模式 | 自动 Content-Type |
| --- | --- | --- |
| 不设置 body | none | 无 |
| `cr_request_set_body` | raw | 不自动设置 |
| `cr_request_set_body_text(..., type)` | raw | `type` 非 NULL 时设置 |
| `cr_request_set_json` | raw | `application/json` |
| `cr_request_add_form` | form | `application/x-www-form-urlencoded` |
| `cr_request_add_file` | multipart | `multipart/form-data; boundary=...` |

规则：

- `cr_request_add_form` 在 raw 模式下返回 `CR_ERR_ARGUMENT`；先调用 `cr_request_set_body(req, NULL, 0)` 清空。
- `cr_request_add_file` 在 raw 模式下同样返回 `CR_ERR_ARGUMENT`。
- `set_body`/`set_body_text`/`set_json` 会清空之前的 form 字段和文件。
- `set_body(NULL, 0)` 会回到 none 模式，并清空 form/file。
- 文件在发送时读取，整个文件会进入内存；路径按 UTF-8 处理。
- `content_type == NULL` 时按扩展名推断，未知扩展名为 `application/octet-stream`。

#### 查询参数编码

```c
cr_request_add_query(req, "q", "hello world");
cr_request_add_query(req, "flag", NULL);
```

请求 URL 为 `...?q=hello%20world&flag`。原有 URL 查询参数会保留，新增参数追加在末尾。

#### 认证优先级

发送时按以下顺序决定 `Authorization`：

1. 请求中显式设置的 `Authorization` 头。
2. `cr_request_set_basic_auth` 或 `cr_request_set_bearer_token`。
3. URL 中的 `user:password@host`（转换为 Basic Auth）。

跨域重定向时会移除 `Authorization`。

### 3.5 发送请求与便捷函数

```c
cr_response *cr_session_send(cr_session *session, cr_request *request);

cr_response *cr_get(cr_session *session, const char *url);
cr_response *cr_head(cr_session *session, const char *url);
cr_response *cr_delete(cr_session *session, const char *url);
cr_response *cr_post(cr_session *session,
                     const char *url,
                     const void *body,
                     size_t len,
                     const char *content_type);
cr_response *cr_post_json(cr_session *session,
                          const char *url,
                          const char *json);
cr_response *cr_post_form(cr_session *session,
                          const char *url,
                          const char *const *names,
                          const char *const *values,
                          size_t count);
```

| 函数 | 说明 |
| --- | --- |
| `cr_session_send` | 发送请求；网络错误也返回 response；request 不被消耗 |
| `cr_get` | GET；`session == NULL` 时使用临时会话 |
| `cr_head` | HEAD |
| `cr_delete` | DELETE |
| `cr_post` | POST 原始 body；`content_type` 可为 NULL |
| `cr_post_json` | POST JSON 字符串；等价于设置 `Content-Type: application/json` |
| `cr_post_form` | POST 表单；`names[i]`/`values[i]` 成对使用 |

`session == NULL` 的便捷函数会创建临时 session，因此**不会保留 Cookie**。需要 Cookie 持久化时，显式创建 `cr_session`。

`cr_session_send` 的行为：

- 成功和网络错误都通常返回非 NULL response。
- 只有在 response 对象分配失败时返回 `NULL`。
- `request` 仍然属于调用方，发送后可以复用。
- 若 `session` 或 `request` 为 `NULL`，返回一个 `CR_ERR_ARGUMENT` response。

### 3.6 Response API

```c
void cr_response_free(cr_response *response);

int cr_response_ok(const cr_response *response);
int cr_response_status(const cr_response *response);
const char *cr_response_reason(const cr_response *response);
int cr_response_http_version(const cr_response *response);

const void *cr_response_body(const cr_response *response, size_t *length);
const char *cr_response_text(const cr_response *response);

const char *cr_response_header(const cr_response *response, const char *name);
size_t cr_response_header_count(const cr_response *response);
const char *cr_response_header_name(const cr_response *response, size_t index);
const char *cr_response_header_value(const cr_response *response, size_t index);

const char *cr_response_url(const cr_response *response);
int cr_response_redirect_count(const cr_response *response);
long long cr_response_elapsed_ms(const cr_response *response);
cr_error cr_response_error(const cr_response *response);
const char *cr_response_error_message(const cr_response *response);
```

| 函数 | 说明 |
| --- | --- |
| `cr_response_free` | 释放 response；允许 `NULL` |
| `cr_response_ok` | 无传输错误且状态码为 2xx/3xx 时返回 1 |
| `cr_response_status` | HTTP 状态码；无响应时为 0 |
| `cr_response_reason` | 原因短语；无值时返回 `""` |
| `cr_response_http_version` | HTTP/1.0 返回 10，HTTP/1.1 返回 11，无响应返回 0 |
| `cr_response_body` | 返回 body 指针并写入字节长度 |
| `cr_response_text` | 把 body 当 NUL 结尾文本返回；可能包含二进制数据 |
| `cr_response_header` | 返回第一个同名响应头；不存在返回 `NULL` |
| `cr_response_header_count` | 响应头数量（包含重复头） |
| `cr_response_header_name` | 按索引取响应头名称 |
| `cr_response_header_value` | 按索引取响应头值 |
| `cr_response_url` | 最终 URL（重定向后）；无值时返回 `""` |
| `cr_response_redirect_count` | 实际跟随的重定向次数 |
| `cr_response_elapsed_ms` | 从发送到响应体读取完成的总耗时，毫秒 |
| `cr_response_error` | `CR_OK` 或错误码 |
| `cr_response_error_message` | 错误说明；成功时为 `""` |

响应头和 body：

- 头名称匹配不区分大小写。
- 重复头（例如多个 `Set-Cookie`）必须用索引遍历，`cr_response_header` 只取第一个。
- body 末尾始终补一个 `NUL`，但长度应以 `cr_response_body` 的 `length` 为准。
- `cr_response_text` 返回的指针在 response 释放前有效。
- 即使读取 body 时发生错误，状态码和已解析的响应头通常仍然可读。

### 3.7 选项继承与默认值

请求级选项初始为“继承”：

| 请求选项 | 未设置时 |
| --- | --- |
| connect timeout | 继承 session |
| read timeout | 继承 session |
| `verify` | 继承 session |
| `follow_redirects` | 继承 session |
| `max_redirects` | 继承 session |
| User-Agent | 使用 session 的 User-Agent |

请求级 `-1` 表示继承；`0` 对超时表示无限等待，对 `verify`/`follow` 表示关闭。

发送时的默认请求头：

| 头 | 默认值 |
| --- | --- |
| `Host` | 由 URL 生成 |
| `User-Agent` | `crequests/1.0.0` |
| `Accept` | `*/*` |
| `Accept-Encoding` | `identity` |
| `Connection` | `close` |
| `Content-Length` | 根据 body 自动计算 |

如果用户显式设置了同名头，通常保留用户值；`Content-Length` 和 multipart 的 `Content-Type` 由库根据实际 body 重算。

### 3.8 重定向

默认跟随 301、302、303、307、308。

| 状态码 | 方法/body 规则 |
| --- | --- |
| 301、302 | POST 改为 GET 并丢弃 body；其他方法保留 |
| 303 | 除 HEAD 外改为 GET 并丢弃 body |
| 307、308 | 保留方法和 body |

其他行为：

- 支持绝对 URL、协议相对 URL（`//host/path`）、绝对路径和相对路径。
- 每次重定向重新计算 Cookie。
- 跨 scheme/host/port 时移除 `Authorization`。
- 达到最大次数时返回 `CR_ERR_TOO_MANY_REDIRECTS`，response 中保留最后一次状态码和错误信息。
- `cr_session_set_follow_redirects(0)` 或请求级 `-1/0` 可关闭跟随。
- `Location` 为空或不存在时视为普通响应，不再跟随。

### 3.9 Cookie

- 自动解析 `Set-Cookie`，并保存到 session。
- 支持 `Domain`、`Path`、`Secure`、`HttpOnly`、`Max-Age`、`Expires`。
- Cookie 名称区分大小写；Domain 匹配不区分大小写。
- `Secure` Cookie 只通过 HTTPS 发送。
- Cookie 数量上限 256。
- 不实现 Public Suffix List。
- `cr_session_clear_cookies` 清空；`cr_session_cookie_count` 返回当前存储数量。
- 便捷函数使用临时 session，因此 Cookie 不会跨调用保留。

### 3.10 请求编码与 wire 行为

- 仅 HTTP/1.1。
- 请求行使用 `HTTP/1.1`，并发送 `Connection: close`。
- 响应体支持：
  - `Content-Length`
  - `Transfer-Encoding: chunked`
  - 无长度时读取到连接关闭
  - `HEAD`、204、205、304 无 body
  - `100 Continue` 和 `101` 特殊处理
- 不检查/解压 `Content-Encoding: gzip`；默认发送 `Accept-Encoding: identity`。
- 默认不启用连接池，每个请求建立新连接。
- 响应头大小上限 1 MiB，单行上限 64 KiB，头数量上限 512。
- 响应体超过 session 的 `max_body_size` 时返回 `CR_ERR_BODY_TOO_LARGE`。

### 3.11 C 示例

```c
#include <stdio.h>
#include "crequests.h"

int main(void)
{
    cr_session *session = cr_session_new();
    cr_request *request = cr_request_new("POST", "https://httpbin.org/post");

    cr_request_add_query(request, "source", "api-doc");
    cr_request_set_header(request, "Accept", "application/json");
    cr_request_set_json(request, "{\"hello\":\"world\"}");
    cr_request_set_timeout(request, 10000, 30000);

    cr_response *response = cr_session_send(session, request);
    if (response == NULL) {
        fprintf(stderr, "out of memory\n");
    } else if (cr_response_error(response) != CR_OK) {
        fprintf(stderr, "request failed: %s\n",
                cr_response_error_message(response));
    } else {
        size_t len = 0;
        const void *body = cr_response_body(response, &len);
        printf("status=%d bytes=%zu\n", cr_response_status(response), len);
        fwrite(body, 1, len, stdout);
        printf("\n");
    }

    cr_response_free(response);
    cr_request_free(request);
    cr_session_free(session);
    return 0;
}
```

---

## 4. C++ API

C++ API 位于命名空间 `cpprequests`，要求 C++17。主要类型：

- `Session`
- `Request`
- `Response`
- `Headers`
- `File`
- `Json` / `JsonError`
- `Error` / `HTTPError`

C++ 封装不改变 C 核心的行为，只增加 RAII、`std::string`、链式调用和 JSON 值类型。

版本宏位于 `cpprequests.hpp`：`CPPREQUESTS_VERSION_MAJOR`、`CPPREQUESTS_VERSION_MINOR`、`CPPREQUESTS_VERSION_PATCH`、`CPPREQUESTS_VERSION_STRING`。

### 4.1 异常

```cpp
namespace cpprequests {

class Error : public std::runtime_error {
public:
    Error(int code, const std::string &message);
    int code() const noexcept;
};

class HTTPError : public Error {
public:
    HTTPError(int status_code, const std::string &url);
    int status_code() const noexcept;
    const std::string &url() const noexcept;
};

class JsonError : public std::runtime_error {
public:
    explicit JsonError(const std::string &message, std::size_t offset = 0);
    std::size_t offset() const noexcept;
};

}
```

| 异常 | 何时抛出 |
| --- | --- |
| `Error` | 参数非法、C API setter 失败；`raise_for_status()` 遇到传输错误 |
| `HTTPError` | `Response::raise_for_status()` 遇到 4xx/5xx；`code()` 为 `-1` |
| `JsonError` | JSON 解析失败、类型不匹配、数组越界、对象键不存在、整数越界 |
| `std::bad_alloc` | `Session` 构造或响应分配失败 |

注意：`Session::send` 遇到网络错误**不抛异常**，而是返回带 `error_code`/`error_message` 的 `Response`。只有无效参数和内存分配失败会直接抛异常。

```cpp
cpprequests::Response r = session.get(url);
r.raise_for_status();  // 传输错误 -> Error；4xx/5xx -> HTTPError
```

### 4.2 Headers 与 File

#### Headers

```cpp
cpprequests::Headers headers = {
    {"Accept", "application/json"},
    {"X-Trace", "abc"}
};
headers.set("Accept", "text/plain");   // 替换所有同名头
headers.add("X-Test", "one");          // 允许重复
headers.add("X-Test", "two");

std::string value = headers.get("accept");
std::vector<std::string> all = headers.get_all("x-test");
bool has = headers.contains("X-Test");
```

特点：

- 大小写不敏感查找。
- 保持插入顺序。
- `add` 允许重复；`set` 先删除同名再添加。
- `get` 返回第一个匹配值或 fallback。
- `find` 返回内部指针；修改 Headers 后可能失效。
- 可迭代；`entries()` 返回 `Entry{name, value}` 列表。

接口：

```cpp
Headers();
Headers(std::initializer_list<std::pair<std::string, std::string>>);
Headers &add(std::string name, std::string value);
Headers &set(std::string name, std::string value);
bool remove(std::string_view name);
void clear() noexcept;
bool contains(std::string_view name) const noexcept;
std::size_t count(std::string_view name) const noexcept;
const std::string *find(std::string_view name) const noexcept;
std::string get(std::string_view name, std::string fallback = {}) const;
std::string operator[](std::string_view name) const;
std::vector<std::string> get_all(std::string_view name) const;
std::size_t size() const noexcept;
bool empty() const noexcept;
const std::vector<Entry> &entries() const noexcept;
```

#### File

```cpp
cpprequests::File file{"avatar", "avatar.png", "image/png"};
```

字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `field` | `std::string` | multipart 字段名 |
| `path` | `std::string` | 本地文件路径 |
| `content_type` | `std::string` | MIME 类型；空字符串表示自动推断 |

### 4.3 Response

`Response` 是字段公开的值类型，拥有自己的字符串和头，可以复制、保存到容器中，不依赖 Session/Request 的生命周期。

```cpp
class Response {  // 字段公开，用法类似 struct
    int status_code;          // HTTP 状态码，0 表示未收到响应
    int http_version;         // 10, 11, 或 0
    int redirect_count;       // 跟随的重定向次数
    long long elapsed_ms;     // 总耗时
    int error_code;           // 0 或 cr_error
    std::string reason;
    std::string url;          // 最终 URL
    std::string content;      // 原始 body 字节
    std::string error_message;
    Headers headers;

    bool ok() const noexcept;
    bool has_error() const noexcept;
    explicit operator bool() const noexcept;

    const std::string &body() const noexcept;
    std::string text() const;

    std::string header(std::string_view name,
                       std::string fallback = {}) const;
    std::vector<std::string> header_values(std::string_view name) const;

    Json json() const;
    Json json_or(Json fallback) const;
    void raise_for_status() const;
};
```

| 成员 | 说明 |
| --- | --- |
| `status_code` | HTTP 状态码 |
| `http_version` | 10/11/0 |
| `redirect_count` | 跟随次数 |
| `elapsed_ms` | 总耗时 |
| `error_code` | 0 或 C 错误码 |
| `reason` | 原因短语 |
| `url` | 最终 URL |
| `content` | 原始 body（可含 NUL） |
| `error_message` | 错误说明 |
| `headers` | 响应头 |
| `ok()` | 无错误且 2xx/3xx |
| `operator bool` | 等价于 `ok()` |
| `body()` | `content` 的 const 引用 |
| `text()` | 返回 `content` 的副本 |
| `header()` | 第一个同名头或 fallback |
| `header_values()` | 所有同名头 |
| `json()` | 解析 body，失败抛 `JsonError` |
| `json_or()` | 解析失败返回 fallback |
| `raise_for_status()` | 传输错误抛 `Error`；4xx/5xx 抛 `HTTPError` |

### 4.4 Request

`Request` 不可复制，可移动，所有 setter 返回 `Request &`，支持链式调用。

```cpp
cpprequests::Request request("POST", "https://example.com/api");
request.set_header("Accept", "application/json")
       .add_query("page", "1")
       .set_json(cpprequests::Json::object({
           {"name", "crequests"},
           {"version", 1}
       }));
```

构造和查询：

```cpp
Request(std::string method, std::string url);
const std::string &method() const noexcept;
const std::string &url() const noexcept;
```

头部：

```cpp
Request &set_header(std::string name, std::string value);
Request &remove_header(std::string_view name);
Request &set_headers(const Headers &headers);
```

查询参数：

```cpp
Request &add_query(std::string name, std::string value);
Request &add_query(std::string name);   // 裸键
```

表单与文件：

```cpp
Request &add_form(std::string name, std::string value);
Request &add_form(const Form &form);
Request &add_file(File file);
Request &add_file(std::string field, std::string path,
                  std::string content_type = {});
```

Body：

```cpp
Request &set_body(std::string body);
Request &set_body(const void *data, std::size_t size);
Request &set_body_text(std::string text, std::string content_type = {});
Request &set_json(const Json &body);
Request &set_json_text(std::string json);
```

- `set_body(std::string)` 发送完整字符串（可含 NUL）。
- `set_body(const void*, size)` 发送任意字节；`data == nullptr && size != 0` 抛 `Error`。
- `set_body_text` 的 `content_type` 为空时不自动设置 Content-Type。
- `set_json(const Json&)` 先 `dump()` 再发送。
- `set_json_text` 原样发送 JSON 字符串，不校验。

认证：

```cpp
Request &basic_auth(std::string username, std::string password = {});
Request &bearer_token(std::string token);
```

选项：

```cpp
Request &set_timeout(int connect_timeout_ms, int read_timeout_ms);
Request &set_verify(bool verify);
Request &set_follow_redirects(bool follow);
Request &set_max_redirects(int max_redirects);
Request &set_user_agent(std::string user_agent);
```

注意：

- `set_timeout(-1, -1)` 恢复继承 session。
- `set_max_redirects(-1)` 恢复继承。
- `set_verify(bool)` 和 `set_follow_redirects(bool)` 总是显式设置，没有“恢复继承”的 C++ 接口；新建 `Request` 即可回到继承状态。
- `Request` 构造时把 method 转成大写。

### 4.5 Session

`Session` 不可复制，可移动。RAII 管理 Cookie jar、默认头和默认选项。

```cpp
cpprequests::Session session;
session.set_header("User-Agent", "my-app/1.0")
        .set_timeout(5000, 20000)
        .set_max_body_size(16 * 1024 * 1024);
```

配置接口：

```cpp
Session();
~Session();

Session &set_header(std::string name, std::string value);
Session &remove_header(std::string_view name);
Session &set_headers(const Headers &headers);

Session &set_timeout(int connect_timeout_ms, int read_timeout_ms);
Session &set_verify(bool verify);
Session &set_follow_redirects(bool follow);
Session &set_max_redirects(int max_redirects);
Session &set_user_agent(std::string user_agent);
Session &set_max_body_size(std::size_t max_bytes);

void clear_cookies();
std::size_t cookie_count() const noexcept;
```

发送接口：

```cpp
Response send(const Request &request);

Response request(std::string method, std::string url);
Response get(std::string url);
Response get(std::string url, const Query &params);
Response get(std::string url, const Query &params, const Headers &headers);
Response head(std::string url);
Response del(std::string url);

Response post(std::string url, std::string body,
              std::string content_type = {});
Response post_json(std::string url, const Json &body);
Response post_form(std::string url, const Form &form);

Response put(std::string url, std::string body,
             std::string content_type = {});
Response patch(std::string url, std::string body,
               std::string content_type = {});
```

- `send` 不消耗 `Request`。
- `get`/`post` 等的 URL 参数按值传入并移动。
- `post`/`put`/`patch` 的 `content_type` 为空时不设置 Content-Type。
- `post_json` 自动设置 JSON Content-Type。
- `post_form` 发送表单编码。

### 4.6 自由函数

```cpp
const char *version() noexcept;
const char *strerror(int code) noexcept;

Response get(std::string url);
Response get(std::string url, const Query &params);
Response get(std::string url, const Query &params, const Headers &headers);
Response head(std::string url);
Response del(std::string url);
Response post(std::string url, std::string body,
              std::string content_type = {});
Response post_json(std::string url, const Json &body);
Response post_form(std::string url, const Form &form);
```

自由函数每次创建临时 `Session`，因此 Cookie 不会跨调用保存。需要持久 Cookie 时使用 `cpprequests::Session`。

### 4.7 JSON

头文件：`#include "cpprequests/json.hpp"`，命名空间 `cpprequests`。`using json = Json;` 也提供了小写别名。

#### 类型

```cpp
using array_type = std::vector<Json>;
using object_item = std::pair<std::string, Json>;
using object_type = std::vector<object_item>;

enum class Json::Type {
    Null,
    Boolean,
    Integer,
    Number,
    String,
    Array,
    Object
};
```

整数和浮点数分别对应 `Integer` 和 `Number`。

#### 构造

```cpp
Json value;                         // null
Json null_value(nullptr);
Json boolean(true);
Json integer(42);
Json number(3.14);
Json string(std::string("hello"));
Json array = Json::array({1, 2, 3});
Json object = Json::object({
    {"name", "crequests"},
    {"version", 1},
    {"ok", true}
});
```

也支持从 `std::string_view`、`array_type`、`object_type`、initializer list 构造。

#### 解析和序列化

```cpp
Json value = Json::parse(R"({"name":"crequests","items":[1,2]})");
std::string compact = value.dump();       // 紧凑
std::string pretty = value.dump(2);       // 2 空格缩进
```

- `parse` 解析完整 JSON 文档，失败抛 `JsonError`；`offset()` 给出出错字节位置。
- 解析深度上限 512。
- `indent < 0` 紧凑输出，`indent >= 0` 美化输出。

#### 类型和访问

```cpp
if (value.is_object()) {
    Json *name = value.find("name");
    if (name != nullptr && name->is_string()) {
        std::cout << name->get_string();
    }
}

std::string name = value.at("name").get<std::string>();
int version = value.value("version", 0);
```

接口包括：

```cpp
Type type() const noexcept;
const char *type_name() const noexcept;
bool is_null/is_boolean/is_integer/is_number/is_string/is_array/is_object() const noexcept;
std::size_t size() const noexcept;
bool empty() const noexcept;

Json *find(std::string_view key) noexcept;
const Json *find(std::string_view key) const noexcept;
bool contains(std::string_view key) const noexcept;

Json &operator[](std::size_t index);
const Json &operator[](std::size_t index) const;
Json &operator[](std::string_view key);
const Json &operator[](std::string_view key) const;

Json &at(std::size_t index);
const Json &at(std::size_t index) const;
Json &at(std::string_view key);
const Json &at(std::string_view key) const;

bool get_boolean() const;
std::int64_t get_integer() const;
double get_number() const;
const std::string &get_string() const;
const array_type &get_array() const;
const object_type &get_object() const;

template <typename T> T get() const;
template <typename T> T value(std::string_view key, T fallback) const;
```

行为：

- 非 const `operator[]` 在 null 上自动创建 array/object；const 版本等价 `at`，缺失会抛 `JsonError`。
- `at` 越界/键不存在抛 `JsonError`。
- `get<T>` 支持 bool、整数、浮点数、`std::string`、`Json`。
- 整数越界抛 `JsonError`。
- `value(key, fallback)` 在缺失、null 或类型不匹配时返回 fallback。

#### 修改

```cpp
value["name"] = "new-name";
value.push_back(Json::object({{"id", 1}}));
value.erase("old-key");
value.erase(0);
value.clear();
```

- `push_back` 在 null 上自动创建数组。
- `erase(std::string_view)` 对 object 删除键，返回是否删除。
- `erase(index)` 删除数组元素，越界抛 `JsonError`。
- `clear()` 清空容器内容；标量变为 null。
- `operator==`/`operator!=` 比较类型和值；整数与整数严格比较，整数与浮点数按数值比较。
- `swap(Json&) noexcept` 交换两个值。

### 4.8 C++ 示例

#### GET + JSON

```cpp
#include <cstdio>
#include "cpprequests.hpp"

int main()
{
    cpprequests::Session session;
    cpprequests::Response r = session.get(
        "https://httpbin.org/get",
        {{"q", "crequests"}});

    if (!r.ok()) {
        std::fprintf(stderr, "%s\n", r.error_message.c_str());
        return 1;
    }
    auto json = r.json();
    std::printf("status=%d, url=%s\n", r.status_code, json.at("url").get<std::string>().c_str());
}
```

#### POST JSON + 异常

```cpp
#include "cpprequests.hpp"

int main()
{
    cpprequests::Json payload = cpprequests::Json::object({
        {"name", "crequests"},
        {"language", "C++17"}
    });

    try {
        cpprequests::Session session;
        cpprequests::Response r =
            session.post_json("https://httpbin.org/post", payload);
        r.raise_for_status();
        auto echoed = r.json().at("json");
        return echoed.at("name").get<std::string>() == "crequests" ? 0 : 1;
    } catch (const cpprequests::HTTPError &e) {
        // 4xx/5xx
    } catch (const cpprequests::Error &e) {
        // 网络/协议/参数错误
    } catch (const cpprequests::JsonError &e) {
        // JSON 解析或类型错误
    }
    return 1;
}
```

---

## 5. 线程、所有权与 ABI

### 5.1 线程安全

- `cr_session` 和 C++ `Session` **不是线程安全的**。
- `cr_request`/`cr_response` 也不保证并发安全。
- 推荐每个线程一个 Session，或由调用方加锁。
- 库内部的 Winsock 初始化是线程安全的。

### 5.2 所有权

C：

- `cr_session_new`/`cr_request_new`/`cr_session_send` 返回的对象归调用方所有。
- 只使用对应的 `cr_*_free` 释放。
- `cr_request` 发送后仍归调用方，可复用。
- `cr_response` 中的 body/header 与 session 无关，可独立持有。

C++：

- `Session`/`Request` 使用 RAII；析构自动释放 C 句柄。
- `Response` 是值类型，拥有 `std::string` 和 `Headers` 副本。
- `Request`/`Session` 不可复制、可移动；移动后源对象处于空状态。

### 5.3 DLL ABI

- C 导出 54 个 `cr_*` 函数；调用约定为默认 cdecl。
- C++ DLL 导出 `cpprequests` 类/函数；不要跨不同编译器/标准库 ABI 混用。
- 调用方与 DLL 必须使用相同架构（x86/x64）。
- 用 `CREQUESTS_SHARED` 或 `CPPR_SHARED` 选择 dllimport。
- 只能用库提供的 free 函数释放库分配的内存。

---

## 6. 限制

- 仅 HTTP/1.1；不支持 HTTP/2、HTTP/3。
- 不自动解压 gzip/br；默认请求 identity。
- 不支持代理、客户端证书、Digest/NTLM、SOCKS。
- 请求体和响应体都在内存中；上传文件整体读入。
- 没有连接池；每个请求使用 `Connection: close`。
- DNS 解析没有独立超时。
- Cookie 实现不包含 Public Suffix List。
- TLS 使用 Windows Schannel 和系统证书存储。
- 当前自动测试主要覆盖 HTTP；TLS 行为需在正常 Windows 桌面环境验证。

---

## 相关文档

- [项目 README](../README.md)
- [C++ 版 README](../cpp/README.md)
- [公共 C 头文件](../include/crequests.h)
- [C++ 头文件](../cpp/include/cpprequests.hpp)
