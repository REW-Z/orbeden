namespace OrbedenEditor;

[Flags]
internal enum SearchableComboAction
{
    None = 0,
    Open = 1,
    Opened = 2,
    SearchChanged = 4,
    Up = 8,
    Down = 16,
    Confirm = 32,
}

/// <summary>保存单个搜索下拉框的搜索词和当前选项。</summary>
public sealed class SearchableComboState
{
    private string search = string.Empty;
    private int currentIndex = -1;
    private List<(int Index, string Label)> results = [];

    //筛选选项并处理下拉框选择
    internal bool Draw(string label, string preview, IReadOnlyList<string> options, out int selectedIndex)
    {
        selectedIndex = -1;
        SearchableComboAction action = NativeEditorGUI.BeginSearchableCombo(label, preview, ref search);
        if (!action.HasFlag(SearchableComboAction.Open)) return false;
        try
        {
            //刷新搜索结果和默认选项
            List<(int Index, string Label)> nextResults = [];
            for (int index = 0; index < options.Count; ++index)
            {
                string option = options[index];
                if (string.IsNullOrWhiteSpace(search) || option.Contains(search, StringComparison.OrdinalIgnoreCase))
                    nextResults.Add((index, option));
            }
            bool reset = (action & (SearchableComboAction.Opened | SearchableComboAction.SearchChanged)) != 0
                || !results.SequenceEqual(nextResults);
            results = nextResults;
            if (reset) currentIndex = results.Count == 0 ? -1 : 0;

            //移动当前选项
            int previousIndex = currentIndex;
            if (!reset && results.Count != 0)
            {
                if (action.HasFlag(SearchableComboAction.Up)) currentIndex = Math.Max(0, currentIndex - 1);
                if (action.HasFlag(SearchableComboAction.Down)) currentIndex = Math.Min(results.Count - 1, currentIndex + 1);
            }
            bool scrollToSelection = reset || previousIndex != currentIndex;

            //绘制结果并确认选择
            if (results.Count == 0) EditorGUI.Label("No results.");
            for (int index = 0; index < results.Count; ++index)
            {
                (int originalIndex, string option) = results[index];
                bool clicked = NativeEditorGUI.SearchableComboItem(option, originalIndex,
                    index == currentIndex, scrollToSelection && index == currentIndex);
                if (clicked) selectedIndex = originalIndex;
            }
            if (selectedIndex < 0 && currentIndex >= 0
                && !action.HasFlag(SearchableComboAction.Opened) && action.HasFlag(SearchableComboAction.Confirm))
                selectedIndex = results[currentIndex].Index;
            if (selectedIndex < 0) return false;
            NativeEditorGUI.ClosePopup();
            return true;
        }
        finally
        {
            NativeEditorGUI.EndChild();
            NativeEditorGUI.EndCombo();
        }
    }
}
