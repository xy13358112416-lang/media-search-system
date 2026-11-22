from pathlib import Path, PurePosixPath
from io import BytesIO
from zipfile import ZipFile
import xml.etree.ElementTree as ET
from PIL import Image
import build_media_defense as deck

ROOT = Path(__file__).resolve().parent
PPTX = ROOT / "media-defense" / "media-defense - 副本.pptx"
OUT = ROOT / "pptx-code-update"
ORIGINAL = OUT / "original"
ORIGINAL.mkdir(parents=True, exist_ok=True)

NS = {
    "p": "http://schemas.openxmlformats.org/presentationml/2006/main",
    "r": "http://schemas.openxmlformats.org/officeDocument/2006/relationships",
    "pr": "http://schemas.openxmlformats.org/package/2006/relationships",
}

def rel_map(xml_bytes):
    root = ET.fromstring(xml_bytes)
    return {el.attrib["Id"]: el.attrib["Target"] for el in root.findall("pr:Relationship", NS)}

def resolve(base, target):
    parts = []
    for part in (PurePosixPath(base).parent / target).parts:
        if part == "..":
            parts.pop()
        elif part != ".":
            parts.append(part)
    return "/".join(parts)

code_pages = {spec["n"] for spec in deck.CODES}
rows = []
with ZipFile(PPTX) as z:
    presentation_rels = rel_map(z.read("ppt/_rels/presentation.xml.rels"))
    presentation = ET.fromstring(z.read("ppt/presentation.xml"))
    slide_paths = []
    for item in presentation.findall("p:sldIdLst/p:sldId", NS):
        rid = item.attrib[f"{{{NS['r']}}}id"]
        slide_paths.append(resolve("ppt/presentation.xml", presentation_rels[rid]))
    for number, slide_path in enumerate(slide_paths, 1):
        if number not in code_pages:
            continue
        rel_path = str(PurePosixPath(slide_path).parent / "_rels" / (PurePosixPath(slide_path).name + ".rels"))
        relationships = ET.fromstring(z.read(rel_path))
        images = []
        for rel in relationships.findall("pr:Relationship", NS):
            if rel.attrib.get("Type", "").endswith("/image"):
                media_path = resolve(slide_path, rel.attrib["Target"])
                data = z.read(media_path)
                image = Image.open(BytesIO(data))
                images.append((media_path, image.size))
                (ORIGINAL / f"code-{number:02d}.png").write_bytes(data)
        if len(images) != 1:
            raise RuntimeError(f"slide {number}: expected one image, got {images}")
        rows.append(f"{number:02d}\t{images[0][0]}\t{images[0][1][0]}x{images[0][1][1]}")

(OUT / "media-map.txt").write_text("\n".join(rows) + "\n", encoding="utf-8")

review = []
for spec in deck.CODES:
    source = (ROOT / spec["file"]).read_text(encoding="utf-8").splitlines()
    review.append(f"\n===== PAGE {spec['n']:02d} | {spec['title']} | {spec['file']} =====")
    selected = set(deck.KEEP.get(spec["n"], []))
    if not selected:
        for start, end in spec["ranges"]:
            selected.update(range(start, end + 1))
    start, end = min(selected), max(selected)
    for line_no in range(start, end + 1):
        marker = "*" if line_no in selected else " "
        review.append(f"{marker}{line_no:4d}: {source[line_no - 1]}")
(OUT / "source-review.txt").write_text("\n".join(review), encoding="utf-8")
print(f"Mapped {len(rows)} code images; source review written to {OUT}")
