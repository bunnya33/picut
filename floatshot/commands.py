"""Screenshot/pin commands used by CLI, hotkeys, corners and tray actions."""
import argparse
import shlex


class CommandParser(argparse.ArgumentParser):
    def error(self, message):
        raise ValueError(message)


def parse_command(command):
    parser = CommandParser(add_help=False, exit_on_error=False)
    parser.add_argument("action", choices=["snip", "paste", "toggle", "restore", "home", "history", "settings", "through"])
    parser.add_argument("--last", action="store_true")
    parser.add_argument("--active", action="store_true")
    parser.add_argument("--full", action="store_true")
    parser.add_argument("--region", nargs=4, type=int, metavar=("X", "Y", "W", "H"))
    parser.add_argument("-o", "--output", choices=["clipboard", "file", "pin"], default="clipboard")
    parser.add_argument("--delay", type=float, default=0)
    parser.add_argument("--file")
    parser.add_argument("--send", type=int)
    parser.add_argument("--group")
    try: args, unknown = parser.parse_known_args(shlex.split(command))
    except argparse.ArgumentError as error: raise ValueError(str(error)) from error
    if unknown or args.delay < 0 or args.delay > 120: raise ValueError("命令参数不正确。")
    return args
