# AI Copilot for Storage — 迭代计划

> 生成日期: 2026-05-18 | 当前版本: v0.1.0 | 计划周期: 4 周 (至 6 月 14 日)

---

## 一、项目当前基线

已实现:
- Legacy Scanner (FindFirstFile) + NTFS Fast Scanner (MFT 直接解析)
- 文件树面板（可展开目录、显示大小/文件数/修改时间）
- RuleEngine（12 条内置规则：node_modules / UE / Docker / pip / Steam / .git 等）
- AI 分析面板（同步调用 DeepSeek，非流式）
- 配置对话框（API Key / Base URL / Model）
- SQLite 缓存（分析结果 + 配置持久化）
- ImGui Docking 布局

已知关键问题:
- **5 个 Critical/High 级 Bug**：悬空指针、NTFS 与 Legacy 扫描结果不一致、字符编码乱码、SQL 静默失败
- **10 个架构/工程债**：AI 调用阻塞主线程、流式 SSE 实装未用、死代码 HttpClient、多 Provider 未实现、Cancel 按钮空实现、规则引擎 O(n²) 递归
- **零测试覆盖**

> 参照 README.md 的项目定位：**"Rules + AI" 架构，核心价值是目录语义理解，护城河是语义知识库。** 当前 MVP 已打通整条流程，但需要修复稳定性 + 补齐核心体验缺口。

---

## 二、四周迭代总览

```
Week 1 (5/19–5/24) 🫛 稳定 —— 清空 Bug 清单，建立 CI，跑通测试框架
Week 2 (5/25–5/31) ⚡ 智能 —— 异步 AI + 流式输出 + 规则引擎升级
Week 3 (6/1–6/7)   🗺️ 可视化 —— Treemap + 搜索筛选 + 扫描进度
Week 4 (6/8–6/14)  🚀 产品化 —— 多 Provider + 扫描历史 + 操作闭环雏形
```

---

## 三、Week 1 —"稳定" (5/19–5/24)

> **目标：消灭所有已知 Bug，搭建 CI + 测试框架，为后续迭代建立安全网。**

### Day 1–2：Critical Bug 修复

| 优先级 | 任务 | 文件 | 描述 |
|--------|------|------|------|
| P0 | 修复悬空指针 | `ui/App.cpp` | 重新扫描后 `selectedNode` / `ruleCacheNode` 置 nullptr；考虑改用索引而非裸指针 |
| P0 | NTFS 扫描器字节数对齐 | `core/NtfsScanner.cpp:735` | `buildSubtree()` 返回子树聚合大小，与 Legacy 的 `totalBytes` 口径一致 |
| P0 | NTFS 根目录计数 +1 | `core/NtfsScanner.cpp:738` | 与 Legacy Scanner 统一为 +1 |
| P1 | 扩展名编码修复 | `core/FileNode.cpp:11-13` | `WideCharToMultiByte(CP_UTF8, ...)` 替代暴力截断 |
| P1 | SQL 静默失败 | `db/Database.cpp:87` | 返回值透传 + 错误日志 |

### Day 3：架构修复

| 任务 | 描述 |
|------|------|
| 删除死代码 | 删除 `src/ai/HttpClient.h/.cpp`（未编译、未引用） |
| Cancel 按钮接线 | 连接 `scanner.cancel()`，扫描中止时 UI 提示 |
| SQLite 错误日志 | `sqlite3_errmsg()` 输出到调试日志 |

### Day 4：CI + 测试基础设施

| 任务 | 描述 |
|------|------|
| GitHub Actions 构建 | PR 触发自动编译 VS2022，输出 build artifact；`generate_vs2022.bat` 支持静默模式 |
| 引入 Google Test | CMake `FetchContent` 集成 gtest，写 1 个示例测试验证链路 |
| MSVC `/W4` clean | 修复所有隐式转换、未初始化变量 warning |

### Day 5–6：核心模块单元测试

| 模块 | 测试内容 |
|------|---------|
| `Scanner` | 在临时目录生成已知文件结构，验证 `totalBytes` / `totalFiles` / `totalDirs` |
| `FileNode` | `extension()` 编码正确性、子节点聚合、`fullPath` 拼接 |
| `RuleEngine` | 12 条规则逐一匹配验证，确保 RiskLevel 输出正确 |
| `Database` | `saveAnalysis` ↔ `loadAnalysis` 往返一致性 |
| `AIClient` | Mock HTTP 响应对 `parseChatResponse` 的正确/异常/截断 JSON 处理 |

### Week 1 交付标准

- [ ] 5 个 P0/P1 Bug 全部修复
- [ ] CI 构建绿灯（每次 PR 自动编译通过）
- [ ] 6 个模块各 ≥ 3 条测试用例通过
- [ ] MSVC `/W4 /WX` 零 warning

---

## 四、Week 2 —"智能" (5/25–5/31)

> **目标：打通 AI 交互体验（异步 + 流式），升级规则引擎，让产品的"AI Copilot"定位名副其实。**

### Day 1–2：AI 调用异步化

| 任务 | 描述 |
|------|------|
| 异步任务队列 | `std::async` + `std::future`，AI 调用脱离主线程；调用期间 UI 显示 "分析中..." 动画 |
| 流式 AI 输出 | UI 调用 `chatStream()` 替代 `chat()`，逐 chunk 追加显示（打字机效果） |
| 超时 + 重试 | 可配置超时（默认 60s），失败自动重试 1 次 |

### Day 3：AI 面板体验升级

| 任务 | 描述 |
|------|------|
| 缓存透出 | 已缓存分析结果标注"来源：缓存"，提供"重新分析"按钮 |
| 分析进度动画 | Spinner / 进度条 + 实时字符计数 |
| 错误友好化 | AI 调用失败时显示具体原因（网络超时 / 认证失败 / 模型不可用），非仅"失败" |

### Day 4–5：规则引擎 v2

> 参照 README 核心观点：**"目录语义知识库才是真正的护城河"**

| 任务 | 描述 |
|------|------|
| 匹配模式升级 | `exact`(精确) / `glob`(`Steam*/steamapps`) / `regex`(`^node_modules$`) / `contains`(向后兼容) |
| 外部 JSON 规则文件 | 从 `rules/` 目录加载 `.json` 规则，内置规则作为 fallback |
| 规则热加载 | 修改规则文件后 UI 中 "Reload Rules" 按钮即时生效 |

### Day 6：规则库扩充

| 任务 | 描述 |
|------|------|
| 从 12 条扩展到 50+ 条 | 新增覆盖：VSCode cache、Chrome cache、WeChat、QQ、Go module cache、Rust cargo、.gradle、.m2、.nuget、conda、cmake-build、.vs、.vscode-server、pipx、ollama models 等 |
| 规则 Schema 文档 | JSON 规则格式说明文档，供社区贡献 |

### Week 2 交付标准

- [ ] AI 分析不阻塞 UI（异步完成）
- [ ] 流式打字机效果正常
- [ ] 规则引擎支持 glob + regex + 外部加载 + 热重载
- [ ] 内置规则 ≥ 50 条
- [ ] 规则文件 Schema 文档完成

---

## 五、Week 3 —"可视化" (6/1–6/7)

> **目标：实现 Treemap + 搜索 + 扫描进度，补齐产品核心体验差。**

### Day 1–3：Treemap 矩形树图

> README 明确将 Treemap 列为核心可视化手段。参照 WinDirStat 的实现思路。

| 任务 | 描述 |
|------|------|
| Squarified Treemap 算法 | 基于文件大小的矩形填充布局，按目录层级递归 |
| ImGui DrawList 绘制 | 利用 `AddRectFilled` + `AddText` 渲染；鼠标悬停高亮 + Tooltip 显示路径与大小 |
| 颜色映射 | 按 RiskLevel / 扩展名 / 文件类型着色 |
| 交互联动 | 点击 Treemap 中的矩形 → 文件树同步选中该节点 |

### Day 4：文件树搜索与筛选

| 任务 | 描述 |
|------|------|
| Ctrl+F 搜索栏 | 文件名模糊匹配，匹配项高亮跳转 |
| 筛选器 | 按扩展名 / 大小范围 / 风险等级 / 最近修改时间过滤 |
| 快速导航 | 搜索结果列表（可点击跳转到树中位置） |

### Day 5：扫描进度实时显示

| 任务 | 描述 |
|------|------|
| 进度回调对接 | 对接 `Scanner::ProgressCallback`，实时显示当前扫描路径 + 文件计数值 |
| 进度条 + ETA | 基于已扫描字节 / 磁盘总容量估算剩余时间 |
| 完成摘要 | 扫描结束弹窗显示：总大小 / 文件数 / 目录数 / 耗时 / Top 5 大目录 |

### Day 6：键盘快捷键

| 快捷键 | 功能 |
|--------|------|
| `Ctrl+O` | 打开文件夹选择 |
| `Ctrl+F` | 聚焦搜索栏 |
| `F5` | 重新扫描当前路径 |
| `Esc` | 取消扫描 / 关闭弹窗 |

### Week 3 交付标准

- [ ] Treemap 可正确显示 ≥ 1000 个目录的矩形布局
- [ ] 搜索响应 &lt; 200ms（10 万节点内）
- [ ] 扫描过程实时显示当前路径和计数
- [ ] 全部键盘快捷键生效

---

## 六、Week 4 —"产品化" (6/8–6/14)

> **目标：多 Provider 切换、扫描历史、配置完善、导出报告，形成可对外分发的 v0.2.0。**

### Day 1–2：多 AI Provider 支持

| 任务 | 描述 |
|------|------|
| OpenAI Provider | 实现 `OpenAIProvider`，复用 `AIClient` 基类（仅 URL + 模型名差异） |
| Ollama Provider | 实现 `OllamaProvider`（本地模型，baseUrl = `http://localhost:11434`） |
| 配置 UI 切换 | Provider 下拉选择 + 自动填充默认 URL；测试连接按钮显示 loading |
| 语言选择 | `PromptBuilder::setLanguage()` 对接配置 UI，支持 zh-CN / en / ja |

### Day 3：扫描历史面板

| 任务 | 描述 |
|------|------|
| 历史列表 | 对接 `Database::loadScanHistory()`，展示时间 / 路径 / 大小 / 耗时 |
| 操作按钮 | "重新加载此记录" / "删除记录" |
| 比较模式 | 选择 2 条历史记录对比差异（新增/删除/增长目录） |

### Day 4：配置完善 + 导出

| 任务 | 描述 |
|------|------|
| 导出 Markdown 报告 | 扫描结果 + AI 分析 + Top 10 大目录 → 格式化的 `.md` 文件 |
| 导出 JSON | 完整扫描数据（文件树结构）导出为 JSON，供外部工具消费 |
| 配置文件 | 将所有配置项整理到配置页面（API、扫描默认路径、语言、主题色） |

### Day 5–6：打磨 + 集成

| 任务 | 描述 |
|------|------|
| 清理队列操作 | 用户可标记多个 Safe 级目录，一键移至回收站（Dry-Run 预览 → 确认 → 执行） |
| 版本号显示 | UI 标题栏显示 v0.2.0；从 git tag 自动注入 |
| 集成测试 | 端到端测试：启动 → 扫描 → AI 分析 → 导出报告 → 退出 |
| 发布准备 | 编译 Release、打包 zip、编写 Release Note、打 tag |

### Week 4 交付标准

- [ ] 可在 DeepSeek / OpenAI / Ollama 三个 Provider 间切换
- [ ] 扫描历史面板完整可用
- [ ] 支持 Markdown + JSON 两种格式导出
- [ ] 清理队列（Dry-Run + 回收站删除）基本可用
- [ ] v0.2.0 发布

---

## 七、优先级判断矩阵

每一期往哪个方向投入，按以下三个维度打分（1-5），取加权总分：

| 维度 | 权重 | 含义 |
|------|------|------|
| **产品价值** | 40% | 对"目录语义理解"这一核心定位的贡献 |
| **用户体验** | 35% | 对新用户第一次使用的感受提升 |
| **工程可持续** | 25% | 降低后续迭代风险、加速开发 |

| 功能方向 | 产品价值 | 用户体验 | 工程可持续 | 加权得分 | 排期 |
|----------|---------|---------|-----------|---------|------|
| Bug 修复 | 4 | 4 | 5 | 4.25 | Week 1 |
| CI + 测试 | 3 | 2 | 5 | 3.15 | Week 1 |
| 异步 AI + 流式 | 4 | 5 | 3 | 4.10 | Week 2 |
| 规则引擎 v2 | 5 | 3 | 4 | 4.15 | Week 2 |
| Treemap | 4 | 5 | 3 | 4.10 | Week 3 |
| 搜索筛选 | 3 | 5 | 3 | 3.75 | Week 3 |
| 多 Provider | 4 | 4 | 4 | 4.00 | Week 4 |
| 扫描历史 | 3 | 4 | 2 | 2.90 | Week 4 |
| 导出报告 | 3 | 3 | 2 | 2.65 | Week 4 |
| 文件操作 | 4 | 4 | 2 | 3.30 | Week 4 |

---

## 八、AI 协作分工

### 🤖 AI 可高比例主导（人仅 Review）

这些任务有明确输入输出、可自动验证、无需物理硬件：

- 单元测试用例编写（Scanner / FileNode / RuleEngine / Database / AIClient / PromptBuilder）
- CI pipeline 配置（GitHub Actions yaml）
- 规则引擎 v2 的模式匹配代码（exact / glob / regex）
- 外部规则文件的解析器（JSON 加载）
- Treemap Squarified 算法的 C++ 实现
- Provider 子类实现（OpenAI / Ollama）
- 导出功能（Markdown / JSON 序列化）
- 键盘快捷键的事件绑定
- 国际化字符串提取

### 👤 人必须主导的任务

这些需要架构决策、安全审计、或在真实多环境下验证：

- NTFS Scanner 修复后的跨卷验证（SSD/HDD/不同簇大小/NTFS 压缩卷）
- 扫描器与 NTFS MFT 边界校验的涉及安全性问题
- 删除/回收站操作的 Windows 版本兼容性测试（Win10 vs Win11）
- 规则优先级和冲突解决策略的设计（需要领域经验判断 System32 等安全边界）
- 性能基线的"可接受劣化阈值"设定

### 日常协作流程

```
人发起 Issue → 人确定接口/边界 → AI 生成代码 + 测试
  → 人 Review + 本地验证 → CI 自动构建
    → 通过 → 人合并
```

---

## 九、技术债务后续追踪（Week 4 后）

以下项目在当前 4 周排期中暂不纳入，但需在 v0.3 之前解决：

| 债务 | 说明 | 建议时机 |
|------|------|---------|
| NTFS USN Journal 增量扫描 | 首扫后增量刷新，秒级更新 | v0.3 |
| 本地模型离线分析 | llama.cpp 集成 | v0.3 |
| 多磁盘 Tab 管理 | 并列扫描对比 | v0.3 |
| React + Tauri UI 重写 | README 规划的 Phase 2 | v0.4+ |
| 国际化完整方案 | 字符串提取 + Crowdin 集成 | v0.4 |
| 存储健康报告 | PDF/HTML 自动生成 | v0.4 |
| 知识库扩展到 200+ 规则 | 社区贡献 + 审核流程 | 持续 |

---

## 十、开发规范

- 分支命名: `feat/week<N>-<name>` (如 `feat/week1-stabilize`)
- 每个独立改动一条 PR，不混入无关修改
- Commit 格式: `fix: 修复 NTFS 扫描器字节数不一致` / `feat: 实现 Treemap Squarified 布局`
- 合并条件: CI 绿灯 + 核心模块测试通过 + 手动冒烟测试通过
- 每日站会检查: 昨天完成了什么 → 今天做什么 → 有无阻塞
