# Hibiki DSP：Codex 視窗與 Goal 啟動詞

本頁提供三種可直接貼用的角色。每個視窗只負責一個可驗證成果，並依 `AGENTS.md` 與
`MULTI_AGENT.md` 擁有獨立 write scope。

## 共同契約

- 修正／實作要求必須完成實作、匹配驗證與整合；不得改選唯讀 audit。
- Issue、branch、PR、CI 是協作／稽核記錄，不是成果或停止點。除非 maintainer 明確要求規劃
  backlog，不建立 candidate、TBD、pre-claim 或排隊 Issue。
- 回報先講產品改了什麼、使用者影響、如何確認與仍缺什麼；Git 細節只在影響結論時放最後。
- 正常終點是 acceptance、fresh exact-head green、ready 與 merge。只有具體 safety、permission、
  scope 或 external blocker 可以暫停，並須留下可接手的 handoff。

## 1. 實作／修正

適用於任何「修、做、完成、繼續實作」要求。

```text
/goal 完成 maintainer 指定的 Hibiki 修正或功能。先讀 AGENTS.md、docs/START_HERE.md、完整 execution Issue handoff 與相關 Spec／ADR，確認 ownership 與 scope 後立即實作；跑 always-run 與變更相符的 gates，誠實區分 source／user-space 與實體音訊／driver 證據。首次可重建 push 後開 draft PR，持續修到 acceptance 通過、fresh exact-head checks 全綠、轉 ready 並合併。只在具體 safety、permission、scope 或 external blocker 時暫停並寫回 handoff；回報先講使用者得到什麼、如何確認與仍缺什麼。
```

## 2. 編排／整合

適用於管理多個已授權 writer 或收斂既有 PR；先交付，再規劃。

```text
/goal 編排並整合 maintainer 已授權的 Hibiki 工作。先讀入口與 active handoffs，先 drain 已 acceptance 通過且 fresh exact-head green 的安全 PR：完成 review、轉 ready、合併與 main readback，再安排新工作。只有 writer 會立即開始時才建立一張欄位完整、scope 不重疊的 execution Issue 並完成 serialized claim；不得建立 candidate、TBD、pre-claim 或排隊 Issue。持續到所有已授權成果整合完成，或遇到具體 safety、permission、scope 或 external blocker；回報以已交付產品能力、驗證與限制為主。
```

## 3. 明確唯讀稽核

只在 maintainer 明確要求 audit／review／status 時使用。若要求包含修正或實作，不得選此角色。

```text
/goal 對 maintainer 指定的 Hibiki 範圍做唯讀稽核。檢查 Spec、source、tests、evidence 與當前 repository／GitHub 狀態，交付 pass／fail 結論、最小重現、使用者影響、證據邊界與建議下一步；不得修改檔案、建立修復 Issue 或把發現排成 backlog，除非 maintainer 另行明確要求規劃。若 maintainer 改為要求修正，切換到「實作／修正」並走完整交付流程。
```
