import argparse
import unreal

parser = argparse.ArgumentParser()
parser.add_argument("--name", required=True)
parser.add_argument("--count", type=int, default=1)
parser.add_argument("--message", default="hello")

args = parser.parse_args()

unreal.log(f"args number = {len(args.__dict__)}")

unreal.log(f"name = {args.name}")
unreal.log(f"count = {args.count}")
unreal.log(f"message = {args.message}")