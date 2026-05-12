# AI 磁盘语义分析工具 —— 项目规划文档

## 一、项目概述

本项目目标并非传统“磁盘清理工具”，而是：

# “AI 驱动的磁盘语义分析系统”

它的核心不只是：

* 找出哪些文件大
* 哪些目录占空间

而是：

# “理解磁盘中的内容”

并进一步：

* 解释这些文件是什么
* 为什么会存在
* 是否可以删除
* 删除后有什么影响
* 是否属于缓存、开发环境、AI模型、游戏资源等
* 如何优化磁盘空间

---

传统工具：

* WinDirStat
* WizTree
* TreeSize

解决的是：

# “空间分布问题”

而本项目目标是：

# “空间语义理解问题”

这是本项目与传统扫盘工具最大的区别。

---

# 二、项目定位

本项目本质上属于：

# “AI Copilot for Storage”

即：

# “磁盘空间智能助手”

而不是传统 Cleaner。

---

项目定位偏向：

* AI辅助系统工具
* 磁盘语义分析
* 本地开发环境分析
* AI/游戏/引擎缓存识别
* 系统垃圾识别
* 用户存储行为分析

---

目标用户包括：

* 开发者
* AI用户
* 游戏用户
* 内容创作者
* 重度PC用户
* 磁盘空间焦虑用户

---

# 三、核心理念

项目核心思想：

# “磁盘中的路径本身具有语义”

例如：

```txt
DerivedDataCache
node_modules
huggingface
Docker
ComfyUI
DXCache
pip cache
```

这些目录名本身已经包含：

* 软件生态
* 用途
* 风险
* 可重建性
* 生命周期

AI 的价值在于：

# “解释这些语义”

而不是单纯扫描文件。

---

因此：

# 真正核心不是“AI聊天”

而是：

# “目录语义理解系统”

---

# 四、项目核心价值

## 1. 解释能力

传统工具：

```txt
占用 43GB
```

本项目：

```txt
这是 Unreal Engine 的 DerivedDataCache。

用于缓存 Shader 与资源数据。

删除后不会影响项目，
但首次重新打开时会重新编译 Shader。

预计可释放 43GB。
```

---

## 2. 风险分析

AI 不是简单：

# “建议删除”

而是：

* 风险等级
* 可重建性
* 是否影响程序
* 是否属于用户数据
* 是否属于开发环境

---

## 3. 用户行为理解

未来可扩展：

* 为什么磁盘越来越大
* 哪类软件占用增长最快
* 哪些缓存长期未使用
* 哪些 AI 模型已经过期
* 哪些项目长期未打开

---

# 五、可借鉴对象分析

---

# 1. WinDirStat

WinDirStat

WinDirStat

特点：

* Treemap
* 文件树
* 类型颜色统计

优点：

* 可视化经典
* 结构清晰

缺点：

* 扫描慢
* 无语义理解
* 无实时能力

---

# 2. WizTree

WizTree

特点：

* 直接读取 NTFS MFT
* 极快扫描速度

这是：

# 扫描层面最重要的参考对象

重点学习：

* MFT解析
* 增量扫描
* IO优化

---

# 3. TreeSize

TreeSize

特点：

* 企业级磁盘分析
* 多种统计方式

可借鉴：

* 信息组织方式
* 多维统计

---

# 4. ChatGPT / Cursor

ChatGPT
Cursor

借鉴：

* AI解释体验
* 流式输出
* 卡片式建议
* Copilot交互模式

---

# 六、技术路线分析

---

# 核心技术架构

```txt
NTFS/MFT Scanner
        ↓
Folder Semantic Analyzer
        ↓
Rule Engine
        ↓
SQLite Knowledge Cache
        ↓
Prompt Builder
        ↓
LLM API
        ↓
Suggestion Engine
        ↓
UI Layer
```

---

# 七、技术栈选择

---

# 1. 后端核心

# C++

原因：

* 文件系统性能
* Windows API
* MFT解析
* 多线程
* IO调度
* 内存控制

适合：

* 高性能系统工具

---

# 2. UI 技术路线

---

## 第一阶段（推荐）

# ImGui

[Dear ImGui GitHub](https://github.com/ocornut/imgui?utm_source=chatgpt.com)

原因：

* 独立开发效率极高
* AI 辅助编程支持优秀
* 实时调试方便
* 非常适合系统工具

用于：

* 原型验证
* 扫描器开发
* Treemap
* 文件树
* AI分析面板

---

## 第二阶段（长期）

# React + Tauri

[Tauri Official Site](https://tauri.app?utm_source=chatgpt.com)

原因：

* AI应用 UI 更适合 Web 技术
* Markdown
* 动画
* 卡片
* 流式输出
* 现代化交互

适合：

* 产品化
* 商业级 UI
* AI Copilot 风格

---

# 3. AI API 接入

推荐：

---

## HTTP

# CPR

[CPR GitHub](https://github.com/libcpr/cpr?utm_source=chatgpt.com)

---

## JSON

# nlohmann/json

[nlohmann json GitHub](https://github.com/nlohmann/json?utm_source=chatgpt.com)

---

## 本地数据库

# SQLite

用于：

* 文件索引
* 历史记录
* AI分析缓存
* embedding
* 用户行为

---

# 八、Prompt 系统设计

Prompt 不是简单“提问”。

而是：

# “定义 AI 如何理解磁盘”

---

# Prompt 三层结构

---

## 1. System Prompt

定义：

* AI身份
* 风险边界
* 输出规则

例如：

* 不允许危险删除建议
* 必须标注风险
* 必须说明原因

---

## 2. Context

结构化目录信息：

```json
{
  "path": "",
  "size_gb": 0,
  "top_extensions": [],
  "largest_subdirs": []
}
```

---

## 3. Task

明确任务：

* 判断用途
* 判断风险
* 给出建议

---

# 九、AI 与规则系统关系

这是整个项目最关键的部分。

---

# AI 不负责：

* 最终删除决策
* 文件识别真实性
* 风险控制

---

# AI 负责：

* 解释
* 总结
* 语义化表达
* 用户建议

---

# 规则系统负责：

* 风险控制
* 目录识别
* 安全边界
* 缓存检测

---

# 正确架构：

# “规则 + AI”

而不是：

# “纯 AI”

---

# 十、初期目标（MVP）

---

# 第一阶段核心：

# “AI解释这个目录是什么”

---

实现：

* 高速扫描
* 文件树
* 大文件分析
* 基础 Treemap
* AI解释目录
* 风险等级
* 删除建议

---

重点：

# 验证“语义理解”价值

而不是：

* UI炫技
* 聊天系统
* Agent

---

# 十一、中期目标

---

加入：

* 增量扫描
* 历史趋势
* 用户行为分析
* 重复文件
* AI模型识别
* 开发环境识别
* Docker/WSL分析
* AI缓存分析

---

UI升级：

* 流式AI响应
* Markdown
* 动画
* 智能建议卡片

---

# 十二、最终目标

最终目标并非：

# “Cleaner”

而是：

# “系统级存储 Copilot”

---

未来能力：

* 自动发现空间异常
* 分析空间增长原因
* 智能归档建议
* 多设备空间分析
* AI存储健康报告
* 个人数字资产理解

---

# 十三、局限性

---

# 1. AI 并不真正理解文件

AI 理解的是：

# “路径语义模式”

因此：

* 必须依赖规则系统
* 必须限制风险

---

# 2. AI 输出不稳定

需要：

* Prompt约束
* JSON结构化输出
* 本地规则校验

---

# 3. Token 成本

不能：

* 上传整个磁盘
* 上传所有文件名

必须：

* 本地预分析
* 提炼摘要

---

# 4. 隐私问题

需要：

* 尽量本地分析
* 避免上传用户敏感路径
* 后期支持本地模型

---

# 十四、项目真正的护城河

最终真正难的：

不是：

* UI
* 扫盘

而是：

# “目录语义知识库”

例如：

* Unreal
* Unity
* Docker
* HuggingFace
* ComfyUI
* Android SDK
* Steam Workshop
* WSL
* Python Cache

这些知识：

# 才是真正长期积累的价值。

---

# 十五、发展展望

未来可能扩展为：

* AI系统维护助手
* AI磁盘健康分析
* AI开发环境管理
* AI模型空间管理
* AI工作站优化工具

---

甚至：

# “Windows AI Storage Layer”

---

# 十六、当前最合理的发展策略

---

# 第一阶段

# C++ + ImGui

快速验证：

* 扫描器
* 规则系统
* Prompt系统
* AI解释能力

---

# 第二阶段

# React/Tauri

升级产品化 UI。

---

# 第三阶段

构建：

# “目录语义知识库”

形成真正核心能力。
