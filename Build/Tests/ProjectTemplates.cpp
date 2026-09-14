#include "Editor/NewProjectTemplate.h"
#include "FileSystem/Utf8Path.h"
#include "Log/Log.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace fs = std::filesystem;

/// <summary>测试中将模板日志输出到控制台。</summary>
void Log::Info(const char* value) { std::cout << value << '\n'; }
/// <summary>测试中保留预期错误，便于定位失败场景。</summary>
void Log::Error(const char* value) { std::cerr << value << '\n'; }

/// <summary>写入测试文件，保留原始字节和行尾。</summary>
void WriteFile(const fs::path& path, const std::string& value)
{
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!output) throw std::runtime_error("Cannot write test fixture");
}

/// <summary>读取测试结果的完整字节。</summary>
std::string ReadFile(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Missing test output: " + Utf8Path::ToUtf8(path));
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

/// <summary>检查实际文件系统行为，不依赖发布构建中的 assert 开关。</summary>
void Require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

/// <summary>验证示例同步与升级脚手架不会误删或覆盖用户内容。</summary>
int main() try
{
    fs::path root = fs::path("Log/ProjectTemplateTests")
        / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    fs::path source = root / "source";
    fs::path target = root / "target";
    NewProjectTemplate::MirrorReport report;
    std::string error;

    //新增、更新和删除只作用于目标树，文本行尾差异不产生更新。
    WriteFile(source / "same.cs", "line\n");
    WriteFile(target / "same.cs", "line\r\n");
    auto sameTime = fs::last_write_time(target / "same.cs");
    WriteFile(source / "changed.world", "new\n");
    WriteFile(target / "changed.world", "old\n");
    WriteFile(source / "new/deep.cs", "new file");
    WriteFile(target / "removed.cs", "stale");
    WriteFile(root / "outside.cs", "keep outside");
    Require(NewProjectTemplate::MirrorTree(source.string(), target.string(), report, error), "Initial mirror failed");
    Require(report.added == 1 && report.updated == 1 && report.removed == 1, "Incorrect mirror report");
    Require(ReadFile(target / "same.cs") == "line\r\n" && fs::last_write_time(target / "same.cs") == sameTime, "Line ending comparison rewrote target");
    Require(ReadFile(target / "changed.world") == "new\n", "Updated bytes changed");
    Require(!fs::exists(target / "removed.cs") && ReadFile(root / "outside.cs") == "keep outside", "Deletion escaped target");
    Require(NewProjectTemplate::MirrorTree(source.string(), target.string(), report, error), "Repeated mirror failed");
    Require(report.added == 0 && report.updated == 0 && report.removed == 0, "Repeated mirror was not a no-op");
    std::cout << "PASS: add/update/delete, source bytes, text line endings and repeated sync\n";

    //未知格式和二进制文件不能被行尾归一化吞掉实际差异。
    WriteFile(source / "asset.custom", "data\r\n");
    WriteFile(target / "asset.custom", "data\n");
    WriteFile(source / "texture.png", std::string("\0\r\n", 3));
    WriteFile(target / "texture.png", std::string("\0\n", 2));
    Require(NewProjectTemplate::MirrorTree(source.string(), target.string(), report, error), "Binary mirror failed");
    Require(report.updated == 2 && ReadFile(target / "asset.custom") == "data\r\n"
        && ReadFile(target / "texture.png") == std::string("\0\r\n", 3), "Binary bytes were normalized");
    std::cout << "PASS: binary and unknown file formats\n";

#ifdef _WIN32
    //Windows 大小写不同的已有文件仍是同一个目标，不能进入删除列表。
    fs::path caseSource = root / "caseSource";
    fs::path caseTarget = root / "caseTarget";
    WriteFile(caseSource / "Scenes/Ground.cs", "updated");
    WriteFile(caseTarget / "scenes/ground.cs", "old");
    Require(NewProjectTemplate::MirrorTree(caseSource.string(), caseTarget.string(), report, error), "Case-only mirror failed");
    Require(report.updated == 1 && report.removed == 0 && ReadFile(caseTarget / "Scenes/Ground.cs") == "updated", "Case-only path was deleted");
    Require(NewProjectTemplate::MirrorTree(caseSource.string(), caseTarget.string(), report, error)
        && report.updated == 0 && report.removed == 0, "Case-only repeated mirror changed files");
    std::cout << "PASS: Windows case-only files and directories\n";
#endif

    //空源、缺失源、路径重叠以及无法复制的文件不触发目标清空。
    fs::create_directories(root / "empty");
    Require(!NewProjectTemplate::MirrorTree((root / "empty").string(), target.string(), report, error), "Empty source accepted");
    Require(!NewProjectTemplate::MirrorTree((root / "missing").string(), target.string(), report, error), "Missing source accepted");
    Require(!NewProjectTemplate::MirrorTree(source.string(), source.string(), report, error), "Identical roots accepted");
    Require(!NewProjectTemplate::MirrorTree(source.string(), (source / "nested").string(), report, error), "Nested target accepted");
    Require(!NewProjectTemplate::MirrorTree(source.string(), root.string(), report, error), "Parent target accepted");
    WriteFile(root / "blockedSource/blocked.cs", "file");
    fs::create_directories(root / "blockedTarget/blocked.cs");
    WriteFile(root / "blockedTarget/stale.cs", "keep on failure");
    Require(!NewProjectTemplate::MirrorTree((root / "blockedSource").string(), (root / "blockedTarget").string(), report, error), "Copy failure was hidden");
    Require(fs::exists(root / "blockedTarget/stale.cs"), "Copy failure continued deleting files");
    Require(fs::exists(target / "new/deep.cs"), "Rejected mirror changed target");
    std::cout << "PASS: missing/empty/overlapping roots and copy failure\n";

    //新建仍复制完整模板并仅重命名工程文件。
    fs::path newProject = root / "newProject";
    Require(NewProjectTemplate::GenerateProjectFiles(newProject.string(), "ReviewGame", "OrbedenEditor/Templates", error), "New project template failed");
    Require(ReadFile(newProject / "ReviewGame.oeproj") == ReadFile("OrbedenEditor/Templates/Project/Project.oeproj"), "New project metadata changed");
    Require(fs::exists(newProject / "ReviewGame.csproj") && fs::exists(newProject / "ReviewGameNative.vcxproj"), "Project filenames not mapped");
    Require(ReadFile(newProject / "Content/Examples/FlightTraining/Scenes/main.world")
        == ReadFile("OrbedenEditor/Templates/Examples/FlightTraining/Scenes/main.world"), "New project scene bytes changed");
    std::cout << "PASS: new project template generation\n";

    //升级保留配置、已改示例和已删内容，且不提前写入模板版本号。
    fs::path upgraded = root / "upgraded";
    const std::string metadata = "<OrbedenProject version=\"2\" startupWorld=\"My.world\"><Layout /></OrbedenProject>";
    WriteFile(upgraded / "ReviewGame.oeproj", metadata);
    WriteFile(upgraded / "Content/Examples/FlightTraining/Scenes/main.world", "user scene");
    WriteFile(upgraded / "Content/Examples/UserScript.cs", "user script");
    WriteFile(upgraded / "Content/Shaders/skybox.orbshader", "user shader");
    Require(NewProjectTemplate::GenerateProjectFiles(upgraded.string(), "ReviewGame", "OrbedenEditor/Templates", error, true), "Upgrade scaffold failed");
    Require(ReadFile(upgraded / "ReviewGame.oeproj") == metadata, "Upgrade overwrote metadata/version");
    Require(ReadFile(upgraded / "Content/Examples/FlightTraining/Scenes/main.world") == "user scene"
        && ReadFile(upgraded / "Content/Examples/UserScript.cs") == "user script", "Upgrade overwrote user examples");
    Require(ReadFile(upgraded / "Content/Shaders/skybox.orbshader") == "user shader"
        && !fs::exists(upgraded / "Content/Shaders/shadow_depth.orbshader"), "Upgrade modified user content");
    Require(!fs::exists(upgraded / "Content/Examples/FlightTraining/Scripts/FlightHud.cs"), "Upgrade restored deleted example");
    Require(fs::exists(upgraded / "ReviewGame.csproj"), "Upgrade did not recreate scaffold");
    fs::path failed = root / "failedUpgrade";
    WriteFile(failed / "ReviewGame.oeproj", metadata);
    fs::create_directories(failed / "ReviewGame.csproj");
    Require(!NewProjectTemplate::GenerateProjectFiles(failed.string(), "ReviewGame", "OrbedenEditor/Templates", error, true), "Upgrade copy failure was hidden");
    Require(ReadFile(failed / "ReviewGame.oeproj") == metadata, "Failed upgrade advanced project version");
    std::cout << "PASS: upgrade preserves user content, deleted files, metadata and failure version\n";
    std::cout << "All project template tests passed. Fixtures: " << root << '\n';
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
