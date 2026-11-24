"""Convert actual SSD1306 framebuffer dumps from ARMCC/Unicorn to PNG and GIF."""
from pathlib import Path
from PIL import Image, ImageDraw
import sys
source, dest = map(Path,sys.argv[1:3])
dest.mkdir(exist_ok=True,parents=True)
data=source.read_bytes()
assert len(data)==36*512
labels=(
'List, initial','Move 0ms','Move 75ms','Move 150ms','Move 225ms','Move 300ms',
'Solid','Round solid','Outline','Round outline','2px bar','Hidden',
'Tiles initial','Tiles intermediate','Tiles settled',
'20px, scroll start','20px, +1s','20px, +1.7s',
'Round modal 0ms','Round modal 70ms','Round modal 140ms','Round modal ready',
'Decimal draft','Decimal committed','Gallery dissolve','GIF and GB2312','Blank page',
'Empty modal','Small viewport','Viewport scrolling','Shape gallery','About device',
'Integer editor','Integer draft','Light switch off','Light switch on'
)
frames=[]
for i in range(len(labels)):
    b=data[512*i:512*(i+1)]
    im=Image.frombytes('L',(128,32),bytes(255 if b[y//8*128+x] & (1<<(y%8)) else 0
                                            for y in range(32) for x in range(128)))
    frames.append(im.resize((384,96),Image.Resampling.NEAREST))
sheet=Image.new('RGB',(4*408,9*126),'#192227')
draw=ImageDraw.Draw(sheet)
for i,(im,label) in enumerate(zip(frames,labels)):
    x=(i%4)*408+12
    y=(i//4)*126+4
    sheet.paste(im,(x,y))
    draw.text((x,y+100),f'{i:02}: {label}',fill='#e2ecee')
sheet.save(dest/'frame-sheet.png')
for tag,seq,dur in (
    ('cursor',range(1,6),75),('tiles',range(12,15),150),('marquee',range(15,18),850),
    ('editor',range(18,24),180),('gallery',range(24,26),160),
    ('viewport',range(28,30),260),('integer',range(32,34),200),('toggle',range(34,36),250)):
    imgs=[frames[i] for i in seq]
    imgs[0].save(dest/(tag+'.gif'),save_all=True,append_images=imgs[1:],duration=dur,loop=0)
