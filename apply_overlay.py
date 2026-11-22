"""Apply this project's overlay to a separate AC79 SDK checkout."""
from pathlib import Path
import argparse, gzip, shutil
p=argparse.ArgumentParser()
p.add_argument('--sdk',required=True,help='Path to the target SDK checkout')
a=p.parse_args()
target=Path(a.sdk).resolve()
if not (target/'include_lib').is_dir() or not (target/'apps').is_dir():
    raise SystemExit('Target must be an existing AC79 SDK checkout')
source=Path(__file__).resolve().parent/'overlay'
count=0
for item in sorted(source.rglob('*')):
    if not item.is_file(): continue
    rel=item.relative_to(source)
    dest=target/rel
    compressed=item.name.endswith('.gz')
    if compressed: dest=dest.with_suffix('')
    dest=dest.resolve()
    if not dest.is_relative_to(target): raise SystemExit('Unsafe overlay path')
    dest.parent.mkdir(parents=True,exist_ok=True)
    if compressed: dest.write_bytes(gzip.decompress(item.read_bytes()))
    else: shutil.copyfile(item,dest)
    count+=1
print('Applied',count,'project files to',target)
