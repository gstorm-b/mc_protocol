# PreToolUse hook for the Agent tool: every subagent, at every nesting level, runs on Sonnet
# (owner decision 2026-09-27, agent-team/project/rules.md, "Subagents").
#
# Second layer of enforcement. The first is the env block of .claude/settings.json
# (CLAUDE_CODE_SUBAGENT_MODEL=sonnet with CLAUDE_CODE_SUBAGENT_MODEL_FORCE=1), which forces the
# model of every subagent that follows the normal model resolution. This hook blocks what that
# setting cannot reach: the fork subagent type, which always inherits the parent's model, and any
# explicit request for another model.
#
# Exit 0 allows the call. Exit 2 blocks it; stderr goes back to the calling agent as the reason.
# Fails closed: input that cannot be read blocks the call.
#
# Wired from .claude/settings.json (untracked; its required content is in agent-team/project/rules.md).

$ErrorActionPreference = 'Stop'

function Block([string]$reason) {
    [Console]::Error.WriteLine("Blocked by agent-team/project/hooks/subagent-model-guard.ps1: $reason " +
        "Every subagent must run on Sonnet (agent-team/project/rules.md, 'Subagents').")
    exit 2
}

try {
    [Console]::InputEncoding = [System.Text.Encoding]::UTF8
    $hookInput = [Console]::In.ReadToEnd() | ConvertFrom-Json
} catch {
    Block "the hook input could not be parsed as JSON."
}

$toolInput = $hookInput.tool_input
if ($null -eq $toolInput) {
    Block "the hook input has no tool_input."
}

if ($toolInput.subagent_type -eq 'fork') {
    Block "subagent_type 'fork' always inherits the parent's model; use a named agent type."
}

$model = $toolInput.model
if ($null -ne $model -and $model -ne '' -and $model -ne 'sonnet') {
    Block "model '$model' was requested; omit model or pass 'sonnet'."
}

exit 0
