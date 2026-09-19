# Tasks: migrate-card-title-search
- [x] 1.1 ICardSearchPage 等价接口(QML property + applyTitleSearch(function))+ 各页实现
  (等价实现:各卡片页暴露 `searchQuery` 属性 + 标题/UP 主不分大小写子串投影;
   MainWindow 注入绑定即天然承担 applyTitleSearch 的提交与切页重放)
- [x] 1.2 MainWindow 搜索框接线(提交/重放/仅卡片页显示)
- [x] 3.1 构建0error+冒烟
- [ ] 3.2 validate+归档+commit
