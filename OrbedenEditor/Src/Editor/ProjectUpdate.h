#pragma once

#include <string>

//按当前模板更新已有项目。
//覆盖项目根下内容根之外的脚手架，并用镜像语义重置 `Content/Builtin/`，最后写入当前项目版本号。
//`Content/` 的其余部分（含 `Examples/`）与 `.oeproj` 原样保留。
namespace ProjectUpdate
{
    //用当前模板更新脚手架与内置内容，最后写入当前项目版本号。
    //版本号写在最后：它写入成功即代表一次完整更新，失败时不写，下次打开仍会提示更新。
    bool UpdateProject(const std::string& projectRoot,
        const std::string& projectName,
        const std::string& templateRoot,
        const std::string& runtimeDllPath,
        std::string& outError);
}
