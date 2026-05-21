---
name: file-reader
description: Read and display the contents of text files.
user-invocable: true
metadata:
  openclaw:
    emoji: "📄"
    requires:
      bins: []
---

# File Reader Skill

Use the `read_file` tool to read the contents of text files.

## Usage

When the user asks to read a file:

```
read_file({"path": "path/to/file.txt"})
```

## Notes

- Only text files are supported
- File size is limited to 10KB
- Binary files will return an error message
