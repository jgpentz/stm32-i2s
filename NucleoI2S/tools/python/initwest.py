# README
# This is a script to replace all instances of SmartCardTestFixture with your project name, and initialize west
# Usage: python3 initwest.py proj_name --help --rename --no_west_setup
# Args:
# * proj_name: required - what this project is named
# * --rename: optional - rename this project, ie making a new project from SmartCardTestFixture
# * --no_west_setup: optional - skip west initialization (I don't know why you would want this but it's here)
# * --help: optional - prints this comment block and exits script before doing anything

import os
import os.path
import sys
import fileinput
from pathlib import Path
from shutil import rmtree

# We're using the minimum packages (ie: not click for parsing commands) so the user
# doesn't need to install anything pre running this script

# This script is intentionally self editing
# Grab directory and remove trailing /
BASE_DIR = "SmartCardTestFixture"


def rename():
    print("Replacing all instances of SmartCardTestFixture with '" + BASE_DIR + "'")
    print("Replacing all instances of SmartCardTestFixture with '" + BASE_DIR.replace("-", "_") + "'")

    # recurse through all files and subdirectories
    for root, subdirs, files in os.walk(BASE_DIR):
        for file in files:
            # If it's .git or .vscode symlink ignore it
            if (
                not root.find(".git") == -1
                or file == ".vscode"
                or not file.find(".png") == -1
                or not root.find("__pycache__") == -1
                or not root.find(".egg-info") == -1
            ):
                continue
            print("replacing ", os.path.join(root, file))
            # Replace every line with SmartCardTestFixture with our new project name
            for line in fileinput.input(os.path.join(root, file), inplace=True):
                # You have to do - and _ because python sometimes can't handle -
                print(
                    line.replace("SmartCardTestFixture", BASE_DIR).replace(
                        "SmartCardTestFixture", BASE_DIR.replace("-", "_")
                    ),
                    end="",
                )
            # Make any new directories we need
            if not root.find("SmartCardTestFixture") == -1:
                if not os.path.exists(root.replace("SmartCardTestFixture", BASE_DIR)):
                    os.mkdir(root.replace("SmartCardTestFixture", BASE_DIR))
            if not root.find("SmartCardTestFixture") == -1:
                if not os.path.exists(root.replace("SmartCardTestFixture", BASE_DIR.replace("-", "_"))):
                    os.mkdir(root.replace("SmartCardTestFixture", BASE_DIR.replace("-", "_")))
            # If the file or directory has SmartCardTestFixture rename it
            os.rename(
                os.path.join(root, file),
                os.path.join(root, file)
                .replace("SmartCardTestFixture", BASE_DIR)
                .replace("SmartCardTestFixture", BASE_DIR.replace("-", "_")),
            )

    westconfig = Path(".west/config")
    if Path.exists(westconfig):
        for line in fileinput.input(westconfig, inplace=True):
            print(
                line.replace("SmartCardTestFixture", BASE_DIR).replace(
                    "SmartCardTestFixture", BASE_DIR.replace("-", "_")
                ),
                end="",
            )

    # Delete all now empty folders
    print("Deleting now empty directories")
    deleted = set()
    for current_dir, subdirs, files in os.walk(BASE_DIR, topdown=False):
        still_has_subdirs = False
        for subdir in subdirs:
            if os.path.join(current_dir, subdir) not in deleted:
                still_has_subdirs = True
                break
        if not any(files) and not still_has_subdirs:
            os.rmdir(current_dir)
            deleted.add(current_dir)

    # Remove remote I don't want your pushes to SmartCardTestFixture
    os.system("cd " + BASE_DIR + " && git remote remove origin && cd ..")


def init_west():
    # Run the actual zephyr west setup
    print("Setting up west environment")
    os.system("pipenv install west")
    os.system("pipenv run west init -l ./" + BASE_DIR)
    os.system("pipenv run west update --narrow -o=--depth=1")
    os.system("pipenv install -r zephyr/scripts/requirements.txt")
    # Symlink vscode settings
    print("Setting up vscode")
    os.system("ln -s " + BASE_DIR + "/.vscode/ .vscode")
    print("Done setting up west")


def cleanup_dir(dir):
    dir = Path(dir)
    if not Path.exists(dir):
        return 0
    rmtree(dir)


def cleanup_file(file):
    file = Path(file)
    if not Path.exists(file):
        return 0
    Path.unlink(file)


def cleanup():
    print("\033[93m ### Beginning cleanup ### \033[0m")
    files = [".vscode", "Pipfile", "Pipfile.lock"]
    directories = [".venv", ".west", "bootloader", "build", "modules", "tools", "zephyr"]
    for file in files:
        cleanup_file(file)
    for dir in directories:
        cleanup_dir(dir)
    print("\033[92m ### Cleanup finished ### \033[0m")


def print_help():
    print(
        """
This is a script to replace all instances of SmartCardTestFixture with your project name, and initialize west
Usage: python3 initwest.py --help --rename --no_west_setup
Args:
* --rename: optional - rename this project
* --no_west_setup: optional - skip initing west
* --cleanup: remove west setup and .venv
* --help: optional - prints this comment block and exits script before doing anything"""
    )


# Don't want to use click because don't want to install anything unfortunately
def parse_args(argv):
    # These are to check they used the script right and run our commands in the proper order because that matters
    skip_west = False
    rename_proj = False
    # skip first argument
    for i in range(1, len(argv)):
        if not argv[i][0:9].find("--rename=") == -1:
            if len(argv[i]) <= len("--rename="):
                print("ERROR: NO RENAME GIVEN")
                exit(1)
            rename_proj = True
            global BASE_DIR
            BASE_DIR = argv[i][9:]
            os.system("mv SmartCardTestFixture " + BASE_DIR)
        elif not argv[i].find("--no_west_setup") == -1:
            skip_west = True
        elif not argv[i].find("--help") == -1:
            print_help()
            exit(0)
        elif not argv[i].find("--cleanup") == -1:
            cleanup()
            exit(0)
        else:
            print("ERROR: argument invalid try --help")
            exit(1)
    if rename_proj:
        rename()
    if not skip_west:
        init_west()

    # install packages
    # os.system("pipenv install -e " + BASE_DIR + "/")

    exit(0)


if __name__ == "__main__":
    parse_args(sys.argv)
