import sys
from pathlib import Path

def print_tree(dir_path, prefix="", ignore_list=None):
    if ignore_list is None:
        # 自动忽略这些常见的干扰庞杂目录/文件
        ignore_list = {'.git', '__pycache__', '.venv', 'build', '.vscode', '.DS_Store'}
    
    path = Path(dir_path)
    
    # 打印根目录名字
    if prefix == "":
        print(f"{path.resolve().name}/")
        
    try:
        # 排序：目录排在前面，文件排在后面，各自按字母排序
        items = sorted(
            [x for x in path.iterdir() if x.name not in ignore_list],
            key=lambda x: (x.is_file(), x.name.lower())
        )
    except PermissionError:
        return

    count = len(items)
    for i, item in enumerate(items):
        is_last = (i == count - 1)
        # 根据是否是最后一个元素决定树枝形状
        connector = "└── " if is_last else "├── "
        
        if item.is_dir():
            print(f"{prefix}{connector}{item.name}/")
            # 下一层目录的缩进前缀
            next_prefix = prefix + ("    " if is_last else "│   ")
            print_tree(item, next_prefix, ignore_list)
        else:
            print(f"{prefix}{connector}{item.name}")

if __name__ == "__main__":
    # 默认探查当前脚本所在目录，也可以通过参数指定路径
    target_path = sys.argv[1] if len(sys.argv) > 1 else "."
    print_tree(target_path)