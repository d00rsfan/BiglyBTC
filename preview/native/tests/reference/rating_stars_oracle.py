# Exact original star composition with real, headless SWT ImageData/PNG/scaling.
from pathlib import Path
import argparse,re,hashlib,json
p=argparse.ArgumentParser();p.add_argument('--source',required=True);p.add_argument('--swt',required=True);p.add_argument('--column',required=True);p.add_argument('--output',required=True);a=p.parse_args();source=Path(a.source);text=source.read_text();begin=re.search(r'public static Image createStarLineImage\s*\(',text).start();brace=text.index('{',begin);depth=0
for end in range(brace,len(text)):
 if text[end]=='{':depth+=1
 elif text[end]=='}':
  depth-=1
  if depth==0:break
body=text[begin:end+1]
column=Path(a.column).read_text();begin=column.index('int cell_width = event.cell.getWidth();');end=column.index('if (updater == null)',begin);click=column[begin:end];assert 'int score =' in click

head='''// Original method; real SWT ImageData, memory-only Image/Display wrappers.
import java.io.*;import java.util.*;import java.security.*;import org.eclipse.swt.graphics.ImageData;import org.eclipse.swt.graphics.Rectangle;import org.eclipse.swt.graphics.RGB;
public final class RatingStarsOracle {
 static class Display{}
 static class Utils{static boolean dark;static boolean isDarkAppearanceNative(){return dark;}}
 static class Image{ImageData data;Image(Display ignored,ImageData value){data=(ImageData)value.clone();}ImageData getImageData(){return (ImageData)data.clone();}Rectangle getBounds(){return new Rectangle(0,0,data.width,data.height);}}
 static Image imgRated,imgUnrated;
 static class ClickCell{int width;int getWidth(){return width;}}
 static class ClickEvent{ClickCell cell=new ClickCell();int x;}
 static class RatingImageUtil{static int starWidth=13;}

'''
foot='''
 static String digest(ImageData data)throws Exception{MessageDigest hash=MessageDigest.getInstance("SHA-256");for(int y=0;y<data.height;y++){for(int x=0;x<data.width;x++){RGB pixel=data.palette.getRGB(data.getPixel(x,y));hash.update((byte)data.getAlpha(x,y));hash.update((byte)pixel.red);hash.update((byte)pixel.green);hash.update((byte)pixel.blue);}}return HexFormat.of().formatHex(hash.digest());}
 public static void main(String[]args)throws Exception{Display display=new Display();imgRated=new Image(display,new ImageData(args[0]+"/rated.png"));imgUnrated=new Image(display,new ImageData(args[0]+"/unrated.png"));try(BufferedReader in=new BufferedReader(new InputStreamReader(System.in,java.nio.charset.StandardCharsets.US_ASCII))){for(String line;(line=in.readLine())!=null;){String[] bits=line.split(" ");if(bits[0].equals("CLICK")){ClickEvent event=new ClickEvent();event.cell.width=Integer.parseInt(bits[1]);event.x=Integer.parseInt(bits[2]);try{System.out.println(click(event));}catch(ArithmeticException failure){System.out.println("NONE");}continue;}float stars=Float.intBitsToFloat(Integer.parseUnsignedInt(bits[0],16)),personal=Float.intBitsToFloat(Integer.parseUnsignedInt(bits[1],16));Utils.dark=bits[2].equals("1");int width=Integer.parseInt(bits[3]);Image result=createStarLineImage(stars,personal,display);if(result==null)throw new IOException("Original star generation failed");ImageData data=result.getImageData();if(width>0&&width<data.width)data=data.scaledTo(width,data.height);System.out.println(data.width+" "+data.height+" "+digest(data));}}}
}
'''
out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True);out.write_text(head+body+'\nstatic int click(ClickEvent event){'+click+'return Math.min(5,score); }\n'+foot);(out.parent/'source-scope.json').write_text(json.dumps({'symbol':'RatingImageUtil.createStarLineImage','source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'method_sha256':hashlib.sha256(body.encode()).hexdigest(),'click_source_sha256':hashlib.sha256(column.encode()).hexdigest(),'click_body_sha256':hashlib.sha256(click.encode()).hexdigest(),'swt_jar_sha256':hashlib.sha256(Path(a.swt).read_bytes()).hexdigest(),'scope':'exact original method with real bundled SWT ImageData PNG/pixel/scaling; memory Image/Display and explicit dark-appearance adapter, no live SWT window/display claim'},indent=2)+'\n')
