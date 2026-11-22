from pathlib import Path, PurePosixPath
from zipfile import ZipFile
import json
import xml.etree.ElementTree as ET
import update_pptx_code_images as update

ROOT=Path(__file__).resolve().parent
BEFORE=ROOT/"media-defense"/".media-defense - 副本.pptx.before.pptx"
AFTER=ROOT/"media-defense"/"media-defense - 副本.pptx"
NS=update.NS

with ZipFile(BEFORE) as before, ZipFile(AFTER) as after:
    if before.namelist()!=after.namelist():
        raise AssertionError("package part list changed")
    rel_root=ET.fromstring(after.read("ppt/_rels/presentation.xml.rels"))
    rels={el.attrib["Id"]:el.attrib["Target"] for el in rel_root.findall("pr:Relationship",NS)}
    pres=ET.fromstring(after.read("ppt/presentation.xml"))
    slide_paths={}
    for number,item in enumerate(pres.findall("p:sldIdLst/p:sldId",NS),1):
        rid=item.attrib[f"{{{NS['r']}}}id"]
        slide_paths[number]=update.resolve("ppt/presentation.xml",rels[rid])
    media_map=json.loads((update.WORK/"media-map.json").read_text(encoding="utf-8"))
    allowed={slide_paths[n] for n in update.SNIPPETS}|set(media_map.values())
    changed={name for name in before.namelist() if before.read(name)!=after.read(name)}
    unexpected=changed-allowed
    missing=allowed-changed
    if unexpected or missing:
        raise AssertionError(f"unexpected={sorted(unexpected)}, missing={sorted(missing)}")
    for n in update.SNIPPETS:
        part=media_map[str(n)]
        expected=(update.NEW/f"code-{n:02d}.png").read_bytes()
        if after.read(part)!=expected:
            raise AssertionError(f"wrong image on slide {n}")
print(f"Verified exactly {len(changed)} intended parts: 47 slide captions and 47 code images; all other parts are byte-identical")
