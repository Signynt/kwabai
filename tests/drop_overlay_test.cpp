#include "../src/probe/drop_overlay.h"
#include <opengl/eglcontext.h>
#include <opengl/egldisplay.h>
#include <opengl/gltexture.h>
#include <opengl/glframebuffer.h>
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>

void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char **argv){QCoreApplication app(argc,argv);try{
    auto display=KWin::EglDisplay::create(eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,nullptr),nullptr);
    if(!display){std::cout<<"SKIP no surfaceless EGL display\n";return 77;}
    auto context=KWin::EglContext::create(display.get(),EGL_NO_CONFIG_KHR,{});
    if(!context){std::cout<<"SKIP no KWin-compatible EGL context\n";return 77;}
    auto texture=KWin::GLTexture::allocate(GL_RGBA8,{240,160});check(bool(texture),"allocate texture");
    KWin::GLFramebuffer fbo(texture.get());check(fbo.valid(),"framebuffer");KWin::GLFramebuffer::pushFramebuffer(&fbo);
    KWin::RenderTarget target(&fbo);KWin::RenderViewport viewport(KWin::RectF(-80,20,120,80),2,target,{});
    DropOverlay overlay;
    auto sample=[&](int x,int y){std::array<unsigned char,4> pixel{};glReadPixels(x,159-y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());return pixel;};
    const QRectF frame(-70,30,90,60);const QColor color=QColor::fromRgba(0x80d75f5f);
    auto clear=[] {glDisable(GL_SCISSOR_TEST);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);};
    auto paint=[&]{overlay.paint(target,viewport,frame,color);};
    clear();paint();const auto border=sample(21,40),fill=sample(60,50),outside=sample(10,10);
    check(border[0]>80&&border[0]<150&&border[1]<70,"ARGB border alpha/color");
    check(fill[0]>10&&fill[0]<40&&fill[1]<20,"translucent fill");check(outside[0]==0,"viewport origin/scale clipping");
    // A fresh scene beneath the overlay produces identical pixels on every
    // drag frame, including frames whose client damage is elsewhere.
    for(int i=0;i<20;++i){clear();paint();check(sample(21,40)==border&&sample(60,50)==fill,"opacity changed across repaints");}
    // The next paint in the stack is the moving window, and must cover the
    // overlay rather than having a screen-end overlay drawn over it.
    glEnable(GL_SCISSOR_TEST);glScissor(40,160-90,80,60);glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
    check(sample(60,50)[2]==255&&sample(60,50)[0]==0,"moving window must cover preview");
    // Preserve the state expected by the following window's renderer.
    glEnable(GL_SCISSOR_TEST);glScissor(3,4,5,6);glDisable(GL_BLEND);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE,GL_ZERO,GL_ONE);paint();
    GLint box[4],factor;glGetIntegerv(GL_SCISSOR_BOX,box);glGetIntegerv(GL_BLEND_SRC_RGB,&factor);
    check(glIsEnabled(GL_SCISSOR_TEST)&&!glIsEnabled(GL_BLEND)&&box[0]==3&&box[1]==4&&box[2]==5&&box[3]==6&&factor==GL_SRC_ALPHA,"GL state not restored");
    clear();overlay.paint(target,viewport,frame,QColor::fromRgba(0x00d75f5f));check(sample(60,50)[0]==0,"zero alpha must hide overlay");
    clear();overlay.paint(target,viewport,frame,QColor::fromRgba(0xffd75f5f),20);
    check(sample(21,21)[0]==0,"rounded corner must be transparent");check(sample(110,21)[0]>150,"top border must remain colored");check(sample(60,50)[0]>0,"rounded interior missing");
    const auto softEdge=sample(25,26);check(softEdge[0]>50&&softEdge[0]<200,"rounded edge must contain fractional pixel coverage without MSAA");
    KWin::RenderViewport normalScale(KWin::RectF(-80,20,240,160),1,target,{});clear();overlay.paint(target,normalScale,frame,QColor::fromRgba(0xffd75f5f),20);check(std::abs(int(sample(15,16)[0])-int(softEdge[0]))<=2,"radius in physical pixels must match at scale 1 and 2");
    clear();overlay.paint(target,viewport,frame,QColor::fromRgba(0xffd75f5f),0);check(sample(21,21)[0]>150,"zero radius must keep square corner");
    check(glGetError()==GL_NO_ERROR,"OpenGL error");KWin::GLFramebuffer::popFramebuffer();
    std::cout<<"PASS offscreen KWin GL overlay: stable alpha, color, negative origin, scale, source occlusion and GL state\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
