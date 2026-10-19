"""Photo display RAM is disposable; the saved source remains unchanged."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include "passport_photo.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
void *__real_malloc(size_t);
void *__real_realloc(void *,size_t);
void __real_free(void *);
static unsigned live,fail_alloc,fail_read,fail_shrink,reads;
static unsigned char saved[PASSPORT_AVATAR_BYTES];
void *__wrap_malloc(size_t size){assert(size==PASSPORT_AVATAR_BYTES);if(fail_alloc){fail_alloc=0;return NULL;}void *p=__real_malloc(size);assert(p);live++;return p;}
void *__wrap_realloc(void *p,size_t n){assert(p&&live==1&&n==PASSPORT_AVATAR_INDEXED_BYTES);if(fail_shrink){fail_shrink=0;return NULL;}return __real_realloc(p,n);}
void __wrap_free(void *p){if(p){assert(live);live--;}__real_free(p);}
static bool read_photo(uint8_t *out,size_t bytes,void *ctx){
 assert(ctx==saved && bytes==sizeof(saved));reads++;
 memcpy(out,saved,bytes);return !fail_read;
}
static void exact_pixels(void){
 assert(passport_avatar && live==1);
 if(!passport_avatar_indexed){assert(passport_avatar_bytes==PASSPORT_AVATAR_BYTES);assert(!memcmp(passport_avatar,saved,sizeof(saved)));return;}
 assert(passport_avatar_bytes==PASSPORT_AVATAR_INDEXED_BYTES);
 for(unsigned i=0;i<PASSPORT_AVATAR_W*PASSPORT_AVATAR_H;i++){
  unsigned index=passport_avatar[PASSPORT_AVATAR_PALETTE_BYTES+i];
  const uint8_t *c=passport_avatar+index*4;assert(c[3]==255);
  unsigned pixel=((c[2]>>3)<<11)|((c[1]>>2)<<5)|(c[0]>>3);
  assert(pixel==(saved[2*i]|(unsigned)saved[2*i+1]<<8));
 }
}
int main(void){
 uint8_t colour[]={0x00,0xf8,0xe0,0x07,0x1f,0x00},original[6];memcpy(original,colour,6);
 passport_photo_apply_theme(colour,6,2);assert(!memcmp(colour,original,6));
 passport_photo_apply_theme(colour,6,1);
 for(unsigned i=0;i<6;i+=2){unsigned v=colour[i]|(unsigned)colour[i+1]<<8;int r=((v>>11)&31)*255/31,g=((v>>5)&63)*255/63,b=(v&31)*255/31;assert(abs(r-g)<9 && abs(b-g)<9);}
 assert(memcmp(colour,original,6));
 memset(saved,0x5a,sizeof(saved));
 for(unsigned cycle=0;cycle<2000;cycle++){
  for(unsigned page=0;page<=PAGE_SETTINGS;page++){
   bool photo=page==PAGE_HOME || page==PAGE_CARD;
   unsigned before=reads;
   assert(passport_photo_load((passport_page_t)page,true,read_photo,saved)==photo);
   assert(reads==before+photo && live==(unsigned)photo);
   if(photo){
    exact_pixels();assert(passport_avatar_indexed&&passport_avatar_allocation==PASSPORT_AVATAR_INDEXED_BYTES);
    assert(!passport_photo_load((passport_page_t)page,true,read_photo,saved));
    assert(live==1 && reads==before+1);
   }else assert(!passport_avatar);
   /* GUI integration separately asserts that old widgets are already gone. */
   passport_photo_release();assert(!live && !passport_avatar && !passport_avatar_bytes && !passport_avatar_allocation && !passport_avatar_indexed);
  }
 }
 assert(!passport_photo_load(PAGE_HOME,false,read_photo,saved));
 assert(!passport_photo_load(PAGE_HOME,true,NULL,saved));
 fail_alloc=1;assert(!passport_photo_load(PAGE_HOME,true,read_photo,saved));assert(!live && !passport_avatar);
 fail_read=1;assert(!passport_photo_load(PAGE_HOME,true,read_photo,saved));assert(!live && !passport_avatar);
 fail_read=0;assert(passport_photo_load(PAGE_CARD,true,read_photo,saved));
 passport_photo_release();passport_photo_release();assert(!live && !passport_avatar);
 for(unsigned i=0;i<sizeof(saved);i++)assert(saved[i]==0x5a);
 /* Every RGB565 value survives the exact palette -> RGB565 round trip. */
 for(unsigned group=0;group<256;group++){
  for(unsigned i=0;i<sizeof(saved)/2;i++){unsigned v=group*256+i%256;saved[2*i]=v;saved[2*i+1]=v>>8;}
  assert(passport_photo_load(PAGE_HOME,true,read_photo,saved));exact_pixels();passport_photo_release();
 }
 /* High-colour legacy uploads remain RGB565, with no lossy quantization. */
 for(unsigned i=0;i<sizeof(saved)/2;i++){saved[2*i]=i;saved[2*i+1]=i>>8;}
 assert(passport_photo_load(PAGE_HOME,true,read_photo,saved));assert(!passport_avatar_indexed);exact_pixels();passport_photo_release();
 memset(saved,0x5a,sizeof(saved));fail_shrink=1;
 assert(passport_photo_load(PAGE_HOME,true,read_photo,saved));
 assert(passport_avatar_indexed&&passport_avatar_allocation==PASSPORT_AVATAR_BYTES);exact_pixels();
 passport_photo_invalidate();assert(passport_photo_refresh_pending()&&passport_avatar&&live==1);
 assert(!passport_photo_load(PAGE_HOME,true,read_photo,saved));
 passport_photo_release();assert(!passport_photo_refresh_pending()&&!live);
 memset(saved,0x3c,sizeof(saved));
 assert(passport_photo_load(PAGE_HOME,true,read_photo,saved));exact_pixels();passport_photo_release();
 passport_photo_invalidate();assert(!passport_photo_load(PAGE_HOME,true,read_photo,saved));passport_photo_release();
 /* Reservation performs no I/O and is reused even when later allocation fails. */
 unsigned before=reads;
 assert(passport_photo_reserve()&&passport_photo_reserved()&&live==1&&reads==before);
 assert(passport_photo_reserve()&&live==1);
 fail_alloc=1;
 assert(passport_photo_load(PAGE_HOME,true,read_photo,saved));
 assert(!passport_photo_reserved()&&reads==before+1);exact_pixels();passport_photo_release();
 assert(!passport_photo_reserve()&&!passport_photo_reserved()&&!live);
 assert(passport_photo_reserve());passport_photo_release();assert(!passport_photo_reserved()&&!live);
 passport_photo_invalidate();assert(!passport_photo_reserve()&&!live);passport_photo_release();
 assert(passport_photo_reserve());fail_read=1;
 assert(!passport_photo_load(PAGE_CARD,true,read_photo,saved));assert(!passport_photo_reserved()&&!live);
 fail_read=0;
 assert(!passport_photo_pack(NULL,PASSPORT_AVATAR_BYTES));
 assert(!passport_photo_pack(saved,PASSPORT_AVATAR_BYTES-1));
 return 0;
}
'''
class PhotoMemoryTest(unittest.TestCase):
    def test_page_ownership_saved_source_and_failures(self):
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'photo.c';binary=Path(directory)/'photo'
            source.write_text(HARNESS)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'main'),str(source),str(ROOT/'main/passport_photo.c'),str(ROOT/'main/passport_photo_pack.c'),'-Wl,--wrap=malloc','-Wl,--wrap=realloc','-Wl,--wrap=free','-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
if __name__=='__main__':unittest.main()
