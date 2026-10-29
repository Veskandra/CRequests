# cpprequests

一个用 C++17 实现的、API 风格接近 Python `requests` 的 HTTP/HTTPS 客户端库，面向 Windows。

它是 [`crequests`](../README.md) 的 C++ 版本：网络、Cookie、重定向、TLS 等行为直接复用经过测试的 C11 核心，因此功能与 C 版保持一致；差别是 C++ 版提供 RAII 类、`std::string`、`Header`/`Json` 值类型，并把原来的“JSON 字符串”升级成真正的 JSON 值。

完整 API 参考见 [`docs/API.md`](../docs/API.md)。

```cpp
#include "cpprequests.hpp"

cpprequests::Response r = cpprequests::get("https://example.com/");
std::printf("%d\n%s\n", r.status_code, r.text().c_str());
```

- C++17，无第三方 HTTP/JSON 库依赖（JSON 也是内置实现）
- 底层 Windows Winsock + Schannel，不依赖 libcurl/OpenSSL
- 静态库、DLL、CMake、Makefile、示例和自动测试
- 241 项自动检查，当前 `241 checks, 0 failures`

## 特性

- `Session`：默认请求头、超时、Cookie jar、TLS 校验、重定向策略、响应体上限
- `Request`：方法、URL、查询参数、请求头、JSON/表单/原始 body、multipart 文件上传
- Basic Auth、Bearer Token、URL 中的 `user:password`
- 自动跟随 301/302/303/307/308；相对 `Location`；302/303 的 POST→GET；跨域移除 `Authorization`
- 响应解析：状态码、原因短语、重复响应头、chunked、`Content-Length`、连接关闭定界、`100 Continue`
- Cookie：持久化到 session；Domain/Path/Secure/HttpOnly/Max-Age/Expires
- 连接超时、读取超时、响应体大小上限
- HTTPS 使用 Windows Schannel，默认校验证书链和主机名
- 类型安全的 JSON：解析、序列化、对象/数组/数字/字符串/布尔/null、Unicode 转义
- 响应 JSON 便捷方法：`Response::json()` / `Response::json_or()`
- 静态库与 DLL 两种构建方式

## 平台与依赖

- Windows 7 及以上，推荐 Windows 10/11
- MinGW-w64 GCC（已在 GCC 16.2 / x86_64 上验证）或 MSVC 2019+（CMake 工程可用）
- C++17；C 核心使用 C11
- 运行时只依赖系统 DLL：`WS2_32.dll`、`Secur32.dll`、`CRYPT32.dll`、`KERNEL32.dll`、`msvcrt.dll`；DLL 构建还依赖 MinGW 的 `libstdc++-6.dll`、`libgcc_s_seh-1.dll`
- 测试需要 Python 3.7+（只用标准库）

> 目前没有 POSIX/Linux 后端。`../src/transport.c` 是 Windows 专用传输层。

## 快速开始

### 1. 静态库

```powershell
cd cpp
make
```

产物：

- `libcpprequests.a`
- `build/get.exe`、`build/post_json.exe`、`build/session.exe`、`build/json_demo.exe`

也可以使用 CMake：

```powershell
cmake -S cpp -B cpp/build-cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build-cmake -j
ctest --test-dir cpp/build-cmake --output-on-failure
```

### 2. DLL

```powershell
cd cpp
make dll
```

产物：

- `libcpprequests.dll`：x64 C++ DLL
- `libcpprequests.dll.a`：MinGW 导入库
- `libcpprequests.a`：静态库（如果之前执行过 `make`）

验证 DLL 导入和实际 HTTP 请求：

```powershell
make dll-test
```

CMake 构建 DLL：

```powershell
cmake -S cpp -B cpp/build-cmake-dll -G "MinGW Makefiles" -DBUILD_SHARED_LIBS=ON
cmake --build cpp/build-cmake-dll -j
ctest --test-dir cpp/build-cmake-dll --output-on-failure
```

#### 使用 DLL

```cpp
#define CPPR_SHARED
#include "cpprequests.hpp"
```

```powershell
g++ app.cpp -Icpp/include -DCPPR_SHARED cpp/libcpprequests.dll.a `
    -lws2_32 -lsecur32 -lcrypt32 -o app.exe
copy cpp/libcpprequests.dll .
.\app.exe
```

部署时 `libcpprequests.dll` 必须和 `app.exe` 同目录（或位于 DLL 搜索路径），并且还需要 MinGW 的 `libstdc++-6.dll`、`libgcc_s_seh-1.dll`。

**ABI 注意**：DLL 导出的是 C++ 类/符号，不是稳定 C ABI。调用方应使用与构建 DLL 相同的架构和兼容的 MinGW 工具链；不要混用 MSVC 编译的调用方。对象由库内函数创建和销毁，不要用 `delete` 释放 `Session`/`Request` 内部对象。

## 代码示例

### GET + 查询参数 + 自定义头

```cpp
#include <cstdio>
#include "cpprequests.hpp"

int main()
{
    cpprequests::Session session;
    session.set_header("X-App", "demo");
    session.set_timeout(10000, 30000);

    cpprequests::Request request("GET", "http://example.com/search");
    request.add_query("q", "hello world")
           .add_query("flag")
           .set_header("Accept", "application/json");

    cpprequests::Response response = session.send(request);
    if (response.has_error()) {
        std::fprintf(stderr, "%s\n", response.error_message.c_str());
        return 1;
    }
    std::printf("%d %s\n", response.status_code, response.reason.c_str());
    std::printf("%s\n", response.text().c_str());
}
```

### POST JSON

```cpp
cpprequests::Json payload = {
    {"name", "cpprequests"},
    {"language", "C++17"},
    {"tags", cpprequests::Json::array({"http", "json"})},
    {"version", 1.0}};

cpprequests::Response response =
    session.post_json("https://httpbin.org/post", payload);

if (response.ok()) {
    cpprequests::Json echoed = response.json();
}
```

`post_json` 会调用 `Json::dump()` 生成 body，并设置 `Content-Type: application/json`。

### 表单 / 文件上传

```cpp
cpprequests::Form form{{"name", "cpprequests"}, {"note", "a b"}};
session.post_form("http://example.com/form", form);

cpprequests::Request upload("POST", "http://example.com/upload");
upload.add_form("name", "cpprequests")
      .add_file("avatar", "avatar.png", "image/png");
session.send(upload);
```

文件在发送时读入内存，以 `multipart/form-data` 发送；`content_type` 传空字符串时按扩展名推断。

### Session 与 Cookie

```cpp
cpprequests::Session session;
session.set_header("User-Agent", "my-app/1.0");
session.set_timeout(5000, 20000);

cpprequests::Response r1 = session.get("http://example.com/login");
cpprequests::Response r2 = session.get("http://example.com/profile");

std::printf("cookies: %zu\n", session.cookie_count());
session.clear_cookies();
```

### 认证

```cpp
cpprequests::Request request("GET", "http://example.com/private");
request.basic_auth("alice", "secret");
// 或
request.bearer_token("eyJhbGciOi...");
```

也可以直接使用 `http://user:pass@host/`，库会转换成 Basic Auth。

### 错误处理

传输/协议错误不会抛异常，而是放在 `Response` 里，和 C 版一致：

```cpp
cpprequests::Response response = session.get(url);
if (response.has_error()) {
    std::fprintf(stderr, "%d: %s\n", response.error_code,
                 response.error_message.c_str());
} else if (!response.ok()) {
    try {
        response.raise_for_status();
    } catch (const cpprequests::HTTPError &error) {
        std::fprintf(stderr, "HTTP %d for %s\n", error.status_code(),
                     error.url().c_str());
    }
}
```

参数错误（非法 URL、非法头、超时值非法等）由 `Request`/`Session` 方法抛出 `cpprequests::Error`；JSON 解析错误抛出 `cpprequests::JsonError`。

## API 概览

完整声明和注释见 [`include/cpprequests.hpp`](include/cpprequests.hpp) 与 [`include/cpprequests/json.hpp`](include/cpprequests/json.hpp)。

### Session

| 方法 | 说明 |
| --- | --- |
| `Session()` / `~Session()` | 创建/销毁会话 |
| `set_header(name, value)` / `remove_header(name)` / `set_headers(headers)` | 默认请求头 |
| `set_timeout(connect_ms, read_ms)` | 毫秒；0 表示无限等待 |
| `set_verify(bool)` | 是否校验证书，默认 true |
| `set_follow_redirects(bool)` / `set_max_redirects(n)` | 重定向策略 |
| `set_user_agent(ua)` | 默认 User-Agent |
| `set_max_body_size(bytes)` | 响应体上限，0 为不限制；默认 64 MiB |
| `clear_cookies()` / `cookie_count()` | Cookie jar |
| `send(request)` | 通过会话发送请求 |
| `get` / `head` / `del` / `post` / `post_json` / `post_form` / `put` / `patch` | 便捷请求 |

### Request

| 方法 | 说明 |
| --- | --- |
| `Request(method, url)` | 创建请求；非法参数抛 `Error` |
| `method()` / `url()` | 当前方法/URL |
| `set_method` / `set_url` | 修改方法/URL |
| `set_header` / `remove_header` / `set_headers` | 请求头 |
| `add_query(name, value)` / `add_query(name)` | 查询参数；后者生成 bare key |
| `add_form(name, value)` / `add_form(form)` | 表单字段 |
| `add_file(file)` / `add_file(field, path, type)` | multipart 文件 |
| `set_body(bytes)` / `set_body(data, size)` | 原始 body |
| `set_body_text(text, type)` | 文本 body；type 为空不改 Content-Type |
| `set_json(Json)` / `set_json_text(raw)` | JSON body |
| `basic_auth(user, pass)` / `bearer_token(token)` | 认证 |
| `set_timeout` / `set_verify` / `set_follow_redirects` / `set_max_redirects` / `set_user_agent` | 单请求覆盖 |

### Response

| 成员 | 说明 |
| --- | --- |
| `status_code` / `reason` / `http_version` | 状态码/原因/HTTP 版本（10/11/0） |
| `headers` / `header(name)` / `header_values(name)` | 响应头，支持重复头 |
| `content` / `text()` / `body()` | 二进制安全的 body 和文本 |
| `url` | 最终 URL |
| `redirect_count` / `elapsed_ms` | 重定向次数/耗时 |
| `error_code` / `error_message` / `has_error()` | 传输/协议错误 |
| `ok()` | 无错误且状态码 2xx/3xx |
| `json()` / `json_or(fallback)` | 解析响应 JSON |
| `raise_for_status()` | 4xx/5xx 抛 `HTTPError`，传输错误抛 `Error` |

### 便捷函数

`cpprequests::get`、`head`、`del`、`post`、`post_json`、`post_form`：创建一个临时 Session，因此 Cookie 不跨调用保留。

## JSON

`cpprequests::Json` 是一个完整的 JSON 值类型：

```cpp
cpprequests::Json value = cpprequests::Json::parse(
    R"({"name":"cpprequests","items":[1,2,3],"ok":true})");

std::string name = value["name"].get<std::string>();
int second = value["items"][1].get<int>();  // 2
bool ok = value["ok"].get<bool>();

value["new"] = cpprequests::Json::array({"a", "b"});
value["count"] = 3;
std::printf("%s\n", value.dump(2).c_str());
```

- 对象保持插入顺序；对象比较与顺序无关
- 支持 null、布尔、64 位整数、double、字符串、数组、对象
- 支持 `\uXXXX`、UTF-16 代理对和 UTF-8 输出
- 严格拒绝尾逗号、前导零、未转义控制字符、NaN/Infinity、尾随字符和过深嵌套
- `dump()` 紧凑输出；`dump(n)` 以 n 空格缩进
- 使用内置实现，无 nlohmann 等第三方依赖
- 解析器默认不校验原始 UTF-8 字节序列（和多数轻量 JSON 库一致）

`Response::json()` 解析失败时抛 `JsonError`；`json_or()` 返回给定 fallback。

## 与 C 版（crequests）的关系

C++ 版把 `crequests` 的 6 个 C 源文件编译进库，C++ 层负责：

- RAII 生命周期（Session/Request 自动释放）
- 异常映射（`Error`）
- STL 容器/字符串
- 类型安全 JSON
- 更符合 C++ 习惯的便捷方法

网络协议、Cookie 规则、重定向细节、TLS 校验都由同一套 C 核心实现，因此两版在这些方面的行为一致。C 版仍然保留在自己的目录，二者互不替换。

## 测试

```powershell
cd cpp
make test
```

测试用 Python 启动本地 HTTP 服务器，覆盖：

- JSON 解析/序列化/Unicode/转义/错误输入
- GET/HEAD/DELETE/POST、查询参数、表单、JSON、multipart
- Basic/Bearer 认证
- 重定向链、相对 Location、POST→GET、307 保 body、跨域移除认证
- Cookie Path/Secure/Max-Age、重复 Set-Cookie、大小写敏感名称
- chunked、204、404、418、100 Continue、重复响应头、UTF-8
- 超时、响应体上限、Content-Length 不完整
- 参数校验、异常类型、DLL 导入和实际 HTTP 请求

当前结果：

```text
241 checks, 0 failures
```

### 关于本机 HTTPS 测试

本机沙箱环境中 Windows 安全包无法取得 Schannel 凭据（`AcquireCredentialsHandle` 返回 `SEC_E_NO_CREDENTIALS`）；系统 `curl.exe`、PowerShell `Invoke-WebRequest`、WinHTTP 在这里也会同样失败。因此自动测试目前只覆盖 HTTP，HTTPS 的 Schannel 代码路径需要在正常桌面会话中验证。证书校验默认开启。

## 项目结构

```text
cpp/
├── include/
│   ├── cpprequests.hpp            公共 C++ API
│   └── cpprequests/json.hpp       JSON 值类型（header-only）
├── src/cpprequests.cpp            C++ 包装层
├── examples/                      get / post_json / session / json_demo
├── tests/
│   ├── test_cpprequests.cpp       HTTP + JSON 测试
│   ├── mock_server.py             本地模拟服务器
│   ├── run_tests.py               测试启动器
│   └── dll_smoke.cpp              DLL 导入测试
├── CMakeLists.txt
├── Makefile
└── README.md
```

C 核心源文件位于 `../src/`，由本项目的构建文件直接编译，不复制一份。

## 限制

- 仅 HTTP/1.1，不支持 HTTP/2
- 不自动解压 gzip/br；默认请求 identity
- 不支持代理、客户端证书、Digest/NTLM 认证、异步请求
- 请求体和响应体都在内存中；大文件上传会整体读入
- 没有连接池（每个请求 `Connection: close`）
- DNS 解析本身没有超时控制
- Cookie 实现不包含 Public Suffix List
- C++ DLL 的 ABI 取决于编译器和标准库版本，不适合作为跨编译器稳定 ABI
- Session/Request 不是线程安全的；每个线程使用独立对象，或由调用方加锁

## License

MIT，见 [LICENSE](LICENSE)。
