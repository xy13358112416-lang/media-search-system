from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
root=Path(__file__).resolve().parent
src=root/'pptx-code-update'/'rendered'
out=root/'pptx-code-update'/'overview'
out.mkdir(exist_ok=True)
font=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',18)
for group in range(4):
    sheet=Image.new('RGB',(1536,960),'#E6EAE7')
    draw=ImageDraw.Draw(sheet)
    for j in range(16):
        n=group*16+j+1
        im=Image.open(src/f'page-{n}.png').convert('RGB')
        im.thumbnail((376,212),Image.Resampling.LANCZOS)
        x=(j%4)*384+4;y=(j//4)*240+25
        sheet.paste(im,(x,y));draw.text((x,y-22),f'{n:02d}',font=font,fill='#18324A')
    path=out/f'overview-{group+1}.jpg';sheet.save(path,quality=88,optimize=True)
    print(path.name,path.stat().st_size)
for n in [5,7,11,27,31,41,54,60]:
    p=src/f'page-{n}.png';print(p.name,p.stat().st_size)
