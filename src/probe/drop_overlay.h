#pragma once
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <opengl/glutils.h>
#include <QRectF>
#include <QColor>
#include <array>
#include <memory>

// One analytic rounded rectangle per pixel: fill/border share coverage and
// never blend over each other. Derivatives antialias both edges at device scale.
class DropOverlay {
public:
    void releaseShader(){shader_.reset();}
    void paint(const KWin::RenderTarget &target,const KWin::RenderViewport &viewport,QRectF frame,QColor color,int radius=0) {
        if(frame.isEmpty()||color.alpha()==0)return;
        if(!shader_)shader_=KWin::ShaderManager::instance()->generateCustomShader(KWin::ShaderTrait::UniformColor|KWin::ShaderTrait::TransformColorspace|KWin::ShaderTrait::RoundedCorners,{},R"(
#version 140
#include "sdf.glsl"
#include "colormanagement.glsl"
uniform vec4 geometryColor;
uniform vec4 box;
uniform vec4 cornerRadius;
uniform float borderWidth;
in vec2 position0;
out vec4 fragColor;
void main() {
    float outer = sdfRoundedBox(position0, box.xy, box.zw, cornerRadius);
    float inner = sdfRoundedBox(position0, box.xy, max(box.zw-vec2(borderWidth),vec2(0.0)), max(cornerRadius-vec4(borderWidth),vec4(0.0)));
    float outerCoverage = 1.0-clamp(0.5+outer/max(fwidth(outer),0.00001),0.0,1.0);
    float innerCoverage = 1.0-clamp(0.5+inner/max(fwidth(inner),0.00001),0.0,1.0);
    vec4 result = geometryColor * outerCoverage * (1.0-0.8*innerCoverage);
    result = encodingToNits(result,sourceNamedTransferFunction,sourceTransferFunctionParams.x,sourceTransferFunctionParams.y);
    result.rgb = (colorimetryTransform*vec4(result.rgb,1.0)).rgb;
    result.rgb = doTonemapping(result.rgb);
    fragColor = nitsToDestinationEncoding(result);
}
)");
        if(!shader_)return;
        GLboolean blend=glIsEnabled(GL_BLEND),scissor=glIsEnabled(GL_SCISSOR_TEST);
        GLint srcRgb,dstRgb,srcAlpha,dstAlpha,eqRgb,eqAlpha,box[4];
        glGetIntegerv(GL_BLEND_SRC_RGB,&srcRgb);glGetIntegerv(GL_BLEND_DST_RGB,&dstRgb);glGetIntegerv(GL_BLEND_SRC_ALPHA,&srcAlpha);glGetIntegerv(GL_BLEND_DST_ALPHA,&dstAlpha);glGetIntegerv(GL_BLEND_EQUATION_RGB,&eqRgb);glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&eqAlpha);glGetIntegerv(GL_SCISSOR_BOX,box);
        glEnable(GL_BLEND);glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        KWin::ShaderBinder binder(shader_.get());auto matrix=viewport.projectionMatrix();matrix.scale(viewport.scale(),viewport.scale());
        shader_->setUniform(KWin::GLShader::Mat4Uniform::ModelViewProjectionMatrix,matrix);shader_->setColorspaceUniforms(KWin::ColorDescription::sRGB,target.colorDescription(),KWin::RenderingIntent::Perceptual);
        shader_->setUniform(KWin::GLShader::ColorUniform::Color,color);
        shader_->setUniform("box",QVector4D(frame.center().x(),frame.center().y(),frame.width()/2,frame.height()/2));
        const float logicalRadius=std::min(double(radius)/viewport.scale(),std::min(frame.width(),frame.height())/2);
        shader_->setUniform("cornerRadius",QVector4D(logicalRadius,logicalRadius,logicalRadius,logicalRadius));shader_->setUniform("borderWidth",float(std::min({2.0,frame.width()/2,frame.height()/2})));
        const double fringe=1.0/viewport.scale();auto r=frame.adjusted(-fringe,-fringe,fringe,fringe).intersected(QRectF(viewport.renderRect()));auto clip=viewport.mapToRenderTarget(KWin::RectF(r)).toRect();
        if(!r.isEmpty()&&!clip.isEmpty()){
            glEnable(GL_SCISSOR_TEST);glScissor(clip.x(),target.size().height()-clip.bottom(),clip.width(),clip.height());
            const std::array<QVector2D,6> vertices{{{float(r.left()),float(r.top())},{float(r.right()),float(r.top())},{float(r.left()),float(r.bottom())},{float(r.left()),float(r.bottom())},{float(r.right()),float(r.top())},{float(r.right()),float(r.bottom())}}};
            auto *buffer=KWin::GLVertexBuffer::streamingBuffer();buffer->reset();buffer->setVertices(vertices);buffer->render(GL_TRIANGLES);
        }
        glBlendEquationSeparate(eqRgb,eqAlpha);glBlendFuncSeparate(srcRgb,dstRgb,srcAlpha,dstAlpha);if(!blend)glDisable(GL_BLEND);
        glScissor(box[0],box[1],box[2],box[3]);if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
    }
private:
    std::unique_ptr<KWin::GLShader> shader_;
};
