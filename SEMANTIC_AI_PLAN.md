# AI Copilot for Storage — 语义 + AI 核心功能专项规划

> 基于 README.md / ITERATION_PLAN.md 及现有代码分析整理
> 生成日期: 2026-05-18 | 当前版本: v0.1.0

---

## 各 Phase 当前进度

| Phase | 名称 | 进度 | 说明 |
|-------|------|------|------|
| **Phase 0** | 地基修复 | ██████████ 100% | 已完成：编码修复、SQLite 日志、死代码删除、错误友好化 |
| **Phase 1** | AI 交互体验升级 | ██████████ 100% | 异步化 + 流式输出 + 超时重试 + 缓存透出 + 进度动画 |
| **Phase 2** | 规则引擎升级 | ██░░░░░░░░ 15% | 12 条硬编码规则存在，但无外部加载/热重载 |
| **Phase 3** | Prompt 系统深度优化 | ░░░░░░░░░░ 0% | 基础三层 Prompt 可用，无版本管理/批量分析 |
| **Phase 4** | 多 Provider + 本地模型 | ░░░░░░░░░░ 0% | 仅 DeepSeek 实现，OpenAI/Ollama 为 stub |
| **Phase 5** | 语义分析与操作闭环 | ░░░░░░░░░░ 0% | 无清理队列，无报告导出，无风险热力图 |

---

## 总体原则

项目核心定位是 **"Rules + AI" 架构**，不是纯 AI。**规则引擎是安全护栏，AI 是语义解释器**。两者缺一不可。

> 核心价值：不是"AI 聊天"，而是**"目录语义理解系统"**

---

## Phase 0 — 地基修复

**目标：让现有 AI 管线正确、稳定地运行**

### 要点

| 任务 | 优先级 | 涉及文件 | 说明 |
|------|--------|----------|------|
| 修复扩展名编码乱码 | P1 | `core/FileNode.cpp:11-13` | `WideCharToMultiByte(CP_UTF8)` 替代 `static_cast<char>` 截断 |
| SQLite 静默失败 | P1 | `db/Database.cpp:87` | `saveAnalysis` 返回值透传 + sqlite3_errmsg 日志 |
| 删除死代码 HttpClient | P2 | `ai/HttpClient.h/.cpp` | WinHTTP 版本从未被编译/引用，CPR 是实际 HTTP 层 |
| AI 错误友好化 | P2 | `ui/App.cpp` | 网络超时/认证失败/模型不可用显示具体原因而非"失败" |

### 达成标准

- [x] 扩展名编码在中文路径下正确
- [x] SQLite 读写错误可从日志追溯
- [x] 死代码已删除，构建无影响
- [x] AI 调用失败时用户可见具体错误原因

### AI 协作注意点

- 编码修复需要对 UTF-16 ↔ UTF-8 转换有基本理解，AI 可以主导
- 死代码删除需要确认没有隐藏引用，让 AI grep 确认后再删
- SQLite 错误日志需要统一日志接口，建议先定义简单宏或函数

---

## Phase 1 — AI 交互体验升级

**目标：AI 分析从"后台黑箱"变为"实时对话式体验"**

### 要点

| 任务 | 说明 |
|------|------|
| **异步化** | `std::async` + `std::future` 将 AI 调用脱离主线程，UI 不卡死 |
| **流式输出** | `chatStream()` 已完成但 UI 未对接，逐 chunk 追加显示（打字机效果） |
| **超时重试** | 默认 60s 超时，失败自动重试 1 次 |
| **缓存透出** | 标注"来源：缓存"，提供"重新分析"按钮 |
| **加载动画** | Spinner + 实时 token 计数 |

### 达成标准

- [x] AI 分析不阻塞 UI（异步完成）
- [x] 流式打字机效果正常
- [x] 超时自动重试，UI 有明确等待状态
- [x] 缓存结果可识别并可强制重新分析

### AI 协作注意点

- **线程安全是关键**：`m_scanRoot` 等状态需加锁或拷贝快照，人必须 Review
- 流式输出拼接时注意 UI 刷新频率控制（每 chunk 刷新 vs 定时刷新 16ms）
- `std::async` 的 future 需在生命周期点 `get()` 避免异常丢失
- 流式 `chatStream()` 已有实现（`DeepSeekProvider.cpp`），UI 对接是主要工作

---

## Phase 2 — 规则引擎升级

**目标：规则引擎从"硬编码 12 条"变为"可扩展知识库"**

### 要点

| 任务 | 说明 |
|------|------|
| **匹配模式升级** | `exact` / `glob` (`Steam*/steamapps`) / `regex` (`^node_modules$`) / `contains`（向后兼容） |
| **外部 JSON 规则** | 从 `rules/` 目录加载 `.json` 规则，内置规则作 fallback |
| **规则热加载** | 修改规则文件后 UI "Reload Rules" 按钮即时生效 |
| **规则库扩充** | 从 12 条 → 50+ 条（VSCode cache, Chrome, WeChat, Go mod, Rust cargo, .gradle, conda 等） |

### 规则 JSON Schema 草案

```json
{
  "rules": [
    {
      "name": "node_modules",
      "category": "development",
      "risk_level": "safe",
      "patterns": [
        {"type": "exact", "value": "node_modules"},
        {"type": "glob", "value": "**/node_modules"}
      ],
      "description": "npm 依赖包目录",
      "can_clean": true,
      "reclaimable_ratio": 1.0,
      "platforms": ["windows", "macos", "linux"]
    }
  ]
}
```

### 达成标准

- [ ] 规则引擎支持 exact / glob / regex / contains 四种匹配
- [ ] 外部 JSON 规则文件可加载，内置规则作 fallback
- [ ] 热重载按钮即时生效
- [ ] 内置规则 ≥ 50 条
- [ ] 规则 JSON Schema 文档完成

### AI 协作注意点

- **这是 AI 最能发挥价值的模块之一**：规则定义、模式匹配代码、JSON 解析器都可以由 AI 主导
- 规则的安全边界需要人判断：`System32`、`Program Files` 等系统目录必须人审
- 建议协作流程：人确定 JSON Schema → AI 批量生成 50+ 条规则数据 → 人 Review 安全敏感规则
- 热加载需要文件监控或手动触发按钮两种方案

---

## Phase 3 — Prompt 系统深度优化

**目标：让 AI 输出更精准、结构化、可消费**

### 要点

| 任务 | 说明 |
|------|------|
| **Prompt 版本管理** | `prompts/` 目录管理多版本 Prompt，可通过配置切换 |
| **上下文增强** | 除 Top 20 扩展名外，加入大文件列表 + 子目录结构摘要 |
| **输出格式约束** | 进一步约束枚举值（`risk_level` 限定 `safe/low/medium/high/critical`） |
| **语言选择** | `PromptBuilder::setLanguage()` 支持 zh-CN / en / ja |
| **批量分析** | 选中多个目录批量提交 AI 分析，合并上下文减少 API 调用 |

### Prompt 三层结构（当前）

```
Layer 1: System Prompt — 定义 AI 身份、风险边界、输出规则
Layer 2: Context — 结构化目录信息（path, size, top_extensions, largest_subdirs）
Layer 3: Task — 明确任务（判断用途、风险、给出建议）
```

### 达成标准

- [ ] AI 输出 JSON 格式 100% 可解析
- [ ] 风险等级枚举值正确（限定在预定义范围内）
- [ ] 支持 zh-CN / en 两种语言
- [ ] 批量分析可正常工作

### AI 协作注意点

- **Prompt 调试是人机协作最密集的环节**：AI 生成 Prompt 草案 → 人 Review → 实际 API 测试 → 迭代
- 建议建立 `prompts/` 目录管理不同版本，用 git 跟踪变更历史
- 上下文大小与 token 成本的平衡需实际测试
- 输出格式约束强烈依赖 `response_format` / `json_mode` 等 API 特性，不同 Provider 支持度不同
- 批量分析需要注意 token 上限（不同模型 context window 不同）

---

## Phase 4 — 多 Provider + 本地模型

**目标：摆脱对单一 Provider 的依赖，支持本地离线分析**

### 要点

| 任务 | 说明 |
|------|------|
| **OpenAI Provider** | 复用 `AIClient` 基类（仅 URL + 模型名差异） |
| **Ollama Provider** | 本地模型支持（`http://localhost:11434`） |
| **Provider 切换 UI** | 下拉选择 + 自动填充默认 URL + 测试连接带 loading |
| **Provider 差异适配** | 不同 API 的 JSON mode / token 限制 / 错误格式 |

### 达成标准

- [ ] 可在 DeepSeek / OpenAI / Ollama 三个 Provider 间切换
- [ ] 每个 Provider 的 `testConnection()` 正常
- [ ] 配置 UI 切换流畅

### AI 协作注意点

- AI 可以主导 Provider 子类实现（继承 `AIClient`，模式非常固定）
- Ollama 需要用户本地先安装，不是项目自身集成 llama.cpp
- 不同 Provider 的 API 兼容性需要人做实际测试：
  - DeepSeek：支持 `json_object` response_format
  - OpenAI：支持 `json_schema` / `json_object`
  - Ollama：通常不支持强制 JSON mode，需 Prompt 约束
- Token 计费逻辑不同：DeepSeek 按 token 计费，Ollama 免费但慢

---

## Phase 5 — 语义分析与操作闭环

**目标：从"只能看"到"能理解 + 能操作"**

### 要点

| 任务 | 说明 |
|------|------|
| **风险热力图** | Treemap 按 AI 分析的 RiskLevel 着色 |
| **清理队列** | 用户标记 Safe 级目录 → Dry-Run 预览 → 确认 → 回收站 |
| **分析结果持久化** | 每次 AI 分析存入 SQLite，支持历史回顾 |
| **导出报告** | 扫描 + AI 分析 → Markdown / JSON 导出 |
| **增量分析** | 首次 AI 分析后，变更文件可增量重新分析 |

### 达成标准

- [ ] Treemap 按风险等级着色，鼠标悬停显示 AI 摘要
- [ ] 清理队列支持 Dry-Run 预览 → 确认 → 执行
- [ ] AI 分析历史可回顾
- [ ] 支持 Markdown + JSON 两种导出格式

### AI 协作注意点

- **回收站操作必须人审**：AI 绝不能自主执行删除，只在 Dry-Run 阶段提供建议
- 安全边界兜底：`C:\Windows`、`C:\Program Files`、`C:\ProgramData` 等系统目录标记为 critical 且锁定不可操作
- 导出功能 AI 可主导（Markdown/JSON 序列化）
- 风险热力图的颜色映射需要人确定（红/橙/黄/绿对应风险等级）

---

## AI 协作分工总表

```
┌──────────────────────────────────────────────────┐
│  AI 高比例主导（人仅 Review）                      │
│                                                   │
│  ✅ 单元测试（Scanner / FileNode / RuleEngine / DB）│
│  ✅ 规则匹配代码（exact / glob / regex）            │
│  ✅ 外部 JSON 规则解析器                           │
│  ✅ 批量生成 50+ 规则数据                          │
│  ✅ Provider 子类实现（OpenAI / Ollama）           │
│  ✅ 导出功能（Markdown / JSON 序列化）              │
│  ✅ Prompt 初稿生成                               │
├──────────────────────────────────────────────────┤
│  人必须主导（AI 辅助）                              │
│                                                   │
│  🛡️ 规则安全边界审查（System32 等）                 │
│  🧵 线程安全审计                                  │
│  🧪 Prompt 实际 API 测试与调优                    │
│  🔌 不同 Provider 兼容性验证                      │
│  🗑️ 删除/回收站操作的安全审计                      │
├──────────────────────────────────────────────────┤
│  最佳协作模式                                      │
│                                                   │
│  人定接口/边界 → AI 生成代码+测试                    │
│    → 人 Review + 本地验证 → CI 自动构建             │
│      → 通过 → 人合并                               │
└──────────────────────────────────────────────────┘
```

---

## 各 Phase 与现有 ITERATION_PLAN 对应关系

| 本规划 Phase | 对应 ITERATION_PLAN | 建议投入周次 |
|--------------|-------------------|------------|
| Phase 0 | Week 1（稳定） | 当前 |
| Phase 1 | Week 2 前半（异步 AI + 流式） | 第 2 周 |
| Phase 2 | Week 2 后半（规则引擎 v2） | 第 2 周 |
| Phase 3 | 穿插在各周中持续迭代 | 第 2-4 周 |
| Phase 4 | Week 4 前半（多 Provider） | 第 4 周 |
| Phase 5 | Week 3-4（Treemap + 清理 + 导出） | 第 3-4 周 |
