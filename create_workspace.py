import os

# Define the base directory
base_dir = "/home/adolf/NeoBenchWorkspace"

# Define the folder structure and files
structure = {
    "roms": ["neorom.ld"],
    "src/neorom/src": ["lib.rs"],
    "src/neofs/src/fs": ["header.rs"],
    "src/neofs/src": ["lib.rs"],
    "src/neorom": ["Cargo.toml"],
    "src/neofs": ["Cargo.toml"],
    "tools": ["build.sh"],
}

# Create the directories and placeholder files
for path, files in structure.items():
    dir_path = os.path.join(base_dir, path)
    os.makedirs(dir_path, exist_ok=True)
    for file in files:
        file_path = os.path.join(dir_path, file)
        with open(file_path, "w") as f:
            f.write("// placeholder\n")

# Create the workspace root files
with open(os.path.join(base_dir, "Cargo.toml"), "w") as f:
    f.write("[workspace]\nmembers = [\n    \"src/neorom\",\n    \"src/neofs\"\n]\n")

with open(os.path.join(base_dir, "README.md"), "w") as f:
    f.write("# NeoBench Workspace\n")
