from pathlib import Path, PurePosixPath
from io import BytesIO
from zipfile import ZipFile, ZIP_DEFLATED
import json
import shutil
import xml.etree.ElementTree as ET
from PIL import Image, ImageDraw, ImageFont
import build_media_defense as deck

ROOT = Path(__file__).resolve().parent
PPTX = ROOT / "media-defense" / "media-defense - 副本.pptx"
WORK = ROOT / "pptx-code-update"
ORIGINAL = WORK / "original"
NEW = WORK / "new"
NEW.mkdir(parents=True, exist_ok=True)

# 每页只保留一段连续的真实源码；不插入省略行。
SNIPPETS = {
    5:(236,240), 6:(133,140), 7:(458,461), 8:(290,297), 9:(1235,1238),
    11:(10,18), 12:(920,927), 13:(439,442), 15:(52,58), 16:(54,60),
    17:(11,18), 18:(128,135), 19:(170,175), 20:(104,109), 22:(8,12),
    23:(42,47), 25:(146,149), 26:(24,29), 27:(800,804), 28:(42,46),
    30:(21,26), 31:(63,66), 32:(65,72), 34:(825,828), 35:(9,14),
    36:(15,22), 37:(84,89), 39:(31,32), 40:(185,187), 41:(34,35),
    42:(428,434), 44:(19,24), 45:(144,146), 46:(195,200), 47:(220,224),
    49:(109,111), 50:(86,88), 51:(62,69), 53:(45,51), 54:(48,54),
    55:(158,164), 57:(9,14), 58:(85,88), 59:(174,180), 60:(224,227),
    62:(17,24), 63:(32,36),
}
CUT_AFTER = {
    41:{34:"source_url TEXT NOT NULL,", 35:"source_url TEXT NOT NULL,"},
}
assert set(SNIPPETS) == {spec["n"] for spec in deck.CODES}

NS = {
    "p":"http://schemas.openxmlformats.org/presentationml/2006/main",
    "r":"http://schemas.openxmlformats.org/officeDocument/2006/relationships",
    "pr":"http://schemas.openxmlformats.org/package/2006/relationships",
}

def resolve(base, target):
    parts=[]
    for part in (PurePosixPath(base).parent / target).parts:
        if part=="..":parts.pop()
        elif part!=".":parts.append(part)
    return "/".join(parts)

def package_media_map(z):
    rel_root=ET.fromstring(z.read("ppt/_rels/presentation.xml.rels"))
    rels={el.attrib["Id"]:el.attrib["Target"] for el in rel_root.findall("pr:Relationship",NS)}
    pres=ET.fromstring(z.read("ppt/presentation.xml"))
    result={}
    for number,item in enumerate(pres.findall("p:sldIdLst/p:sldId",NS),1):
        if number not in SNIPPETS:continue
        slide=resolve("ppt/presentation.xml",rels[item.attrib[f"{{{NS['r']}}}id"]])
        rel_path=str(PurePosixPath(slide).parent/"_rels"/(PurePosixPath(slide).name+".rels"))
        slide_rels=ET.fromstring(z.read(rel_path))
        media=[resolve(slide,el.attrib["Target"]) for el in slide_rels.findall("pr:Relationship",NS) if el.attrib.get("Type","").endswith("/image")]
        if len(media)!=1:raise RuntimeError(f"slide {number}: expected one code image, got {media}")
        result[number]=media[0]
    return result

def split_short_code(draw,s,size,max_width):
    if not s:return [""]
    result,current=[],""
    for ch in s:
        if current and deck.text_width(draw,current+ch,size)>max_width:
            cut=max(current.rfind(" "),current.rfind(","),current.rfind(";"),current.rfind("("))
            if cut>=max(8,int(len(current)*0.25)):
                result.append(current[:cut+1]);current="        "+current[cut+1:]
            else:result.append(current);current="        "
        current+=ch
    result.append(current)
    return result

def render(spec, size):
    start,end=SNIPPETS[spec["n"]]
    source=(ROOT/spec["file"]).read_text(encoding="utf-8").splitlines()
    chosen=[(n,source[n-1].rstrip()) for n in range(start,end+1) if source[n-1].strip()]
    common=min(len(s)-len(s.lstrip()) for _,s in chosen)
    prepared=[]
    for n,s in chosen:
        value=s[common:].rstrip()
        marker=CUT_AFTER.get(spec["n"],{}).get(n)
        if marker and marker in value:value=value[:value.index(marker)+len(marker)]
        prepared.append((n,value))
    iw,ih=size;code_size=34
    measure=ImageDraw.Draw(Image.new("RGB",(iw,100),deck.WHITE))
    rows=[]
    for n,s in prepared:
        chunks=split_short_code(measure,s,code_size,iw-124)
        rows.extend((n if j==0 else None,ch,n in spec["hi"]) for j,ch in enumerate(chunks))
    max_rows=(ih-96)//36
    if len(rows)>max_rows:raise ValueError(f"page {spec['n']}: {len(rows)} rows exceeds {max_rows}")
    row_h=min(46,(ih-96)//len(rows))
    image=Image.new("RGB",(iw,ih),deck.WHITE);d=ImageDraw.Draw(image)
    d.rectangle((0,0,iw-1,ih-1),outline="#D2DEDA",width=2)
    d.rectangle((0,0,iw,61),fill=deck.PALE)
    d.text((24,14),f"{spec['file']}  ·  {start}–{end} 行",font=ImageFont.truetype(deck.CJK_FONT,26),fill=deck.TEAL)
    y=74
    for n,s,hi in rows:
        if hi:d.rectangle((60,y-4,iw-18,y+row_h-5),fill="#E4EFEA")
        if n is not None:
            nf=ImageFont.truetype(deck.CODE_FONT,23);label=str(n)
            d.text((48-d.textlength(label,font=nf),y+4),label,font=nf,fill="#849189")
        deck.draw_mixed(d,(82,y),s,code_size,deck.TEAL if hi else deck.TEXT)
        y+=row_h
    path=NEW/f"code-{spec['n']:02d}.png"
    image.save(path,optimize=True)
    return len(rows),row_h

def render_all():
    with ZipFile(PPTX) as z:
        media_map=package_media_map(z)
        report=[]
        for spec in deck.CODES:
            n=spec["n"];data=z.read(media_map[n]);size=Image.open(BytesIO(data)).size
            rows,row_h=render(spec,size)
            report.append(f"{n:02d}\t{SNIPPETS[n][0]}-{SNIPPETS[n][1]}\t{rows} rows\trow_h={row_h}\t{size[0]}x{size[1]}")
        (WORK/"render-report.txt").write_text("\n".join(report)+"\n",encoding="utf-8")
        (WORK/"media-map.json").write_text(json.dumps(media_map,ensure_ascii=False,indent=2),encoding="utf-8")
    ops=[{"slide":n,"find":"真实代码摘录 · ... 表示省略 · 蓝绿色底为关键行","replace":"真实代码片段 · 蓝绿色底为关键行"} for n in sorted(SNIPPETS)]
    ops_path=ROOT/"media-defense"/".media-defense - 副本.pptx.ops.json"
    ops_path.write_text(json.dumps(ops,ensure_ascii=False,indent=2),encoding="utf-8")
    print(f"Rendered {len(report)} short code images without ellipsis markers")

def patch_images():
    media_map=json.loads((WORK/"media-map.json").read_text(encoding="utf-8"))
    replacements={media_map[str(n)]:(NEW/f"code-{n:02d}.png").read_bytes() for n in SNIPPETS}
    tmp=PPTX.with_name(PPTX.name+".tmp")
    with ZipFile(PPTX,"r") as src, ZipFile(tmp,"w") as dst:
        names=set(src.namelist())
        if not set(replacements).issubset(names):raise RuntimeError("code media part missing")
        for info in src.infolist():
            data=replacements.get(info.filename,src.read(info.filename))
            dst.writestr(info,data)
    with ZipFile(tmp) as check:
        if check.testzip() is not None:raise RuntimeError("ZIP CRC check failed")
        for part,expected in replacements.items():
            if check.read(part)!=expected:raise RuntimeError(f"image verification failed: {part}")
    tmp.replace(PPTX)
    print(f"Replaced {len(replacements)} code images in {PPTX}")

if __name__=="__main__":
    import sys
    if len(sys.argv)==2 and sys.argv[1]=="patch":patch_images()
    else:render_all()
