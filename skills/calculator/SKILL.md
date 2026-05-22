---
name: calculator
description: Evaluate mathematical expressions using the calculator tool.
user-invocable: true
command-dispatch: tool
command-tool: calculator
metadata:
  openclaw:
    emoji: "🧮"
    requires:
      bins: []
---

# Calculator Skill

Use the `calculator` tool to evaluate mathematical expressions.

## Usage

When the user asks you to calculate something, use the calculator tool:

```
calculator({"expr": "3 + 5 * 2"})
```

## Supported Operations

- Addition: `+`
- Subtraction: `-`
- Multiplication: `*`
- Division: `/`
- Parentheses: `()`
- Power: `^`
