# Hibiki DSP：Codex 視窗與 Goal 啟動詞

本頁給 maintainer 開新 Codex 視窗時直接貼用。每個視窗只負責一個可驗證成果；同時開多個視窗
時，先依 `AGENTS.md` 與 `MULTI_AGENT.md` 分開 write scope，不讓兩個 writer 改同一份契約或檔案。

Codex 的 `/goal` 適合跨多輪長任務，但不是「永遠從待辦清單隨便挑下一件」。Goal 要寫清楚成果、
邊界、驗證與停止條件。官方用法見 [Long-running work](https://learn.chatgpt.com/docs/long-running-work)
與 [Follow a goal](https://learn.chatgpt.com/use-cases/follow-goals)。

## 所有視窗共同的說話方式

回報依下列順序，標題可以省略，但意思不能省略：

1. **現在在做什麼**：用一般使用者聽得懂的方式說明產品能力，不先報 Git 操作。
2. **你會感覺到什麼**：說明完成後的操作、聲音、穩定性或風險差異。
3. **怎麼確認**：只列最有意義的測試、畫面、量測或可重現結果。
4. **還差什麼**：誠實區分已完成、只在模擬環境成立，以及需要真機／使用者決定的部分。
5. **開發紀錄（必要時）**：branch、PR、commit、CI 放最後一小段；除非 maintainer 詢問，否則不逐項播報。

遇到專有名詞時，第一次出現就順手翻成白話。內部仍須完整遵守 handoff、scope、驗證與 evidence
規則；只是不要把維護者不關心的 Git 流程當成對話主角。

## 五個主要視窗

### 1. 長駐 V1 總管（最長時間使用）

這個視窗負責排序、拆分、整合與維持「目前到底完成到哪裡」的真實狀態。它可以跑很久，但終點
仍是可驗證的 V1 驗收，而不是無限制擴張產品。

```text
/goal 把 Hibiki DSP 推進到 accepted V1 驗收條目全部可重跑通過；先讀 AGENTS.md、docs/START_HERE.md、docs/AI_HANDOFF.md、docs/state/BASELINE.md 與 active handoffs，建立或整理可驗證的 V1 缺口，每輪只選一個不與其他視窗重疊、可獨立驗收的最高價值里程碑，完成協調與驗證後再進下一輪；只有尚未分配且 write scope 明確歸本視窗的工作才自行實作或整合，不搶占已分配的 UI、音訊核心、Windows／driver 或驗證範圍。每次先用白話說產品變好了什麼、使用者會感覺到什麼、怎麼確認與還缺什麼，GitHub 細節只放末尾短記錄。除非全部 V1 驗收通過，或遇到需要我決定、外部帳號／付費／硬體、安裝或簽章、會改變機器狀態、scope 衝突或無法安全前進的阻擋，否則持續工作。
```

### 2. 介面與操作體驗

```text
/goal 完成 maintainer 或長駐 V1 總管明確分配給這個視窗的一個具名、可獨立驗收的 Hibiki 介面／操作體驗里程碑；先讀專案入口、active handoff 與對應 UI Spec，確認自己的範圍不與其他視窗重疊，只修改 apps／control model 與被明列的 UI 文件或測試，不順手改音訊核心或 driver。以正式 WinUI 畫面、鍵盤／螢幕閱讀器操作、狀態回饋與相關 checks 證明成果；先用白話回報使用者現在能更容易做什麼、畫面或操作改善在哪裡、還有哪些真機限制，GitHub 細節只放最後。達成該里程碑的 acceptance 後停止；若尚未有具名分配、需要產品取捨、正式 target 環境或 scope 擴張，先停下說明。
```

### 3. 音訊核心與 DSP

```text
/goal 完成 maintainer 或長駐 V1 總管明確分配給這個視窗的一個具名、可獨立驗收的音訊核心／DSP 里程碑；先讀專案入口、active handoff、相關 Spec／ADR、source、tests 與 evidence，確認自己的範圍不與其他視窗重疊，只修改已認領的 src／tests／契約範圍並守住 RT thread 不配置、不鎖、不等待的硬限制。用可重現測試、量測與失敗邊界證明聲音處理或穩定性改善；先用白話回報聲音或可靠性改了什麼、使用者何時會受益、尚未證明什麼，GitHub 細節只放最後。該里程碑通過驗收後停止；若尚未有具名分配，或需要改公開契約、DSP 順序或跨 lane 決策，先停下協調。
```

### 4. Windows 系統整合與 driver

```text
/goal 解決 maintainer 或長駐 V1 總管明確分配給這個視窗的一個具名、可重現的 Windows 音訊整合或 driver 阻擋，直到取得對應層級的通過證據或收斂成可驗證的最小根因；先讀專案入口、active handoff、driver／Windows runtime 的 Spec、官方文件、source、tests 與既有 evidence，確認自己的範圍不與其他視窗重疊。先做 source、build、模擬或隔離 VM 能安全完成的工作；沒有我明確同意，不申請 Microsoft Hardware 帳號、不購買憑證、不啟用 TESTSIGNING、不安裝或載入 driver，也不改真實裝置設定。先用白話回報 Windows 現在卡在哪一層、修好後使用者會得到什麼、證據能證明到哪裡，GitHub 細節只放最後。取得該阻擋的驗收證據後停止；若尚未有具名分配，或需要外部帳號、硬體、簽章、安裝或改變機器狀態，停下列出最小決定。
```

### 5. 獨立驗證與找錯

```text
/goal 獨立驗證 maintainer 或長駐 V1 總管明確分配給這個視窗的一個具名 V1 里程碑，確認目前 main 是否真的能讓使用者完成預期工作；先讀專案入口、該里程碑的 Spec、source、tests、evidence 與已知限制，以唯讀檢查、可重現操作和範圍相符的 gates 找出誤報、退化、隱私風險或缺少的證據。預設不修改 feature source，也不接手別人的修復範圍；若發現問題，先交付最小重現、使用者影響、嚴重度與建議下一步。回報先講功能到底能不能用、在哪種情境失敗與怎麼重現，GitHub 細節只放最後。完成一份有 pass／fail 結論與證據的驗證報告後停止；若尚未有具名分配就先停下，只有 maintainer 另行指派修復時才開始寫入。
```

## 怎麼開最實用

- 平常先開 **長駐 V1 總管**，由它把當前缺口整理成具名里程碑，再開一至三個專門視窗；不必五個全開。
- 專門視窗負責做深，總管負責避免重複、確認成果能接回產品。
- 想問進度時直接問「現在產品多了什麼、還差什麼？」；不需要追問 branch 或 PR。
- Goal 跑完一個具名成果就讓它停止、換下一個 Goal；不要把互不相關的 backlog 塞進同一個 Goal。
