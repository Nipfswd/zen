import os
import fileinput

def is_header_missing(f):
    with open(f) as reader:
        lines = reader.read().lstrip().splitlines()
        if len(lines) > 0: return not lines[0].startswith("// ")
        return True

def add_headers(files, header):
    for line in fileinput.input(files, inplace=True):
        if fileinput.isfirstline():
            [ print(h) for h in header.splitlines() ]
        print(line, end="")

def scan_tree(root):
    files = []
    header_files = []
    with os.scandir(root) as dirs:
        for entry in dirs:
            if entry.is_dir():
                scan_tree(os.path.join(root, entry.name))
            elif entry.name.endswith(".cpp") or entry.name.endswith(".h"):
                print("... formatting: %s"%(entry.name))
                full_path = os.path.join(root, entry.name)
                files.append(full_path)
                if is_header_missing(full_path):
                    header_files.append(full_path)
    args = ""
    if files:
        os.system("clang-format -i " + " ".join(files))
    if header_files:
        add_headers(header_files, "// Copyright Noah Games, Inc. All Rights Reserved.\n\n")

def scan_zen(root):
    with os.scandir(root) as dirs:
        for entry in dirs:
            if entry.is_dir() and entry.name.startswith("zen"):
                    scan_tree(os.path.join(root, entry.name))

while True:
    if (os.path.isfile(".clang-format")):
        scan_zen(".")
        quit()
    else:
        cwd = os.getcwd()
        if os.path.dirname(cwd) == cwd:
            quit()
        os.chdir("..")