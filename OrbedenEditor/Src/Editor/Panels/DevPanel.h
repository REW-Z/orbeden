#pragma once

#include "Editor/Panels/IEditorPanel.h"

#include <string>

class EditorSystem;

//开发面板：只在引擎开发期用得上的操作，默认隐藏，从 Views 菜单打开。
//当前放的是示例写回：把项目里迭代出来的示例内容刷回源码树里的模板。
class DevPanel : public IEditorPanel
{
private:
    EditorSystem& editor;
    EditorPanelInfo info;
    std::string report;

public:
    explicit DevPanel(EditorSystem& owner);

    //获取面板信息
    const EditorPanelInfo& GetPanelInfo() const override;

    //绘制面板内容
    void DrawPanel() override;

private:
    //项目里的示例目录：<项目根>/Content/Examples
    std::string GetProjectExamplesPath() const;

    //源码树里的模板示例目录：<仓库根>/OrbedenEditor/Templates/Examples
    std::string GetTemplateExamplesPath() const;

    //把项目里迭代的示例写回模板，模板随后是新的基准。
    void WriteBackExamples();

    //用模板重铺项目里的示例，丢弃项目里的改动。
    void ResetExamplesFromTemplate();
};
