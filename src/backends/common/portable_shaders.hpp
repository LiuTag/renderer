/**
 * @file
 * @brief 生成 PBR、阴影、ToneMap、FXAA 与加权 OIT 的可移植 GLSL，不作为 SDK 导出。
 */
#ifndef RENDERER_SRC_BACKENDS_COMMON_PORTABLE_SHADERS_HPP_INCLUDED
#define RENDERER_SRC_BACKENDS_COMMON_PORTABLE_SHADERS_HPP_INCLUDED
#include <string>

namespace renderer::detail {
// 共享着色器 ABI：group 0 为帧/灯光/阴影，group 1 为材质，group 2 为后处理输入。
// 以下仍是 Vulkan 语义 GLSL，进入 OpenGL 前由 ShaderCompiler 统一编译和转译。
inline const std::string frameBlock = R"(
layout(std140,set=0,binding=0) uniform FrameData {
    mat4 viewProjection;
    vec4 lightDirection;
    vec4 ambient;
    mat4 lightViewProjection;
    vec4 cameraPosition;
    vec4 shadowParameters;
    vec4 postParameters;
    vec4 counts;
    mat4 inverseViewProjection;
    vec4 colorMultiplySaturation;
    vec4 colorAdd;
    vec4 contactParameters;
} frame;
)";
inline const std::string materialBlock = R"(
layout(std140,set=1,binding=0) uniform MaterialData {
    vec4 baseColor;
    vec4 factors;
    vec4 emissive;
    vec4 misc;
} material;
layout(set=1,binding=1) uniform sampler2D baseMap;
)";
/**
 * @brief 生成场景或阴影顶点着色器源码。
 * @param shadow true 时使用光源视投影，false 时使用相机视投影。
 * @return 使用统一绑定约定的 GLSL 源码文本。
 */
inline std::string sceneVertex(bool shadow) {
    return std::string("#version 450\n") + frameBlock + R"(
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texcoord;
layout(location=3) in mat4 world;
layout(location=7) in mat3 normalMatrix;
layout(location=10) in vec4 flags;
layout(location=11) in vec4 uvRect;
layout(location=12) in vec4 tint;
layout(location=13) in vec4 colorOverlay;
layout(location=14) in vec4 outlineColor;
layout(location=15) in vec4 effects;
layout(location=0) out vec3 worldPosition;
layout(location=1) out vec3 worldNormal;
layout(location=2) out vec2 uv;
layout(location=3) flat out vec4 instanceFlags;
layout(location=4) flat out vec4 instanceTint;
layout(location=5) flat out vec4 instanceOverlay;
layout(location=6) flat out vec4 instanceOutline;
layout(location=7) flat out vec4 instanceEffects;
layout(location=8) flat out vec4 instanceRect;
void main(){
    vec4 p=world*vec4(position,1);
    worldPosition=p.xyz;worldNormal=normalMatrix*normal;
    uv=texcoord*uvRect.zw+uvRect.xy;instanceFlags=flags;instanceTint=tint;
    instanceOverlay=colorOverlay;instanceOutline=outlineColor;instanceEffects=effects;instanceRect=uvRect;
    gl_Position=)" +
           (shadow ? "frame.lightViewProjection" : "frame.viewProjection") + R"(*p;
}
)";
}
/**
 * @brief 生成支持 Alpha Mask 的阴影片元着色器源码。
 * @return 使用统一绑定约定的 GLSL 源码文本。
 */
inline std::string shadowFragment() {
    // Alpha Mask 的裁剪规则与主场景保持一致，使镂空区域不会投出实心阴影。
    return std::string("#version 450\n") + materialBlock + R"(
layout(location=2) in vec2 uv;
layout(location=4) flat in vec4 instanceTint;
void main(){if(material.misc.y>0.5 && texture(baseMap,uv).a*material.baseColor.a*instanceTint.a<material.factors.z)discard;}
)";
}
/**
 * @brief 生成常规或加权 OIT 的 PBR 片元着色器源码。
 * @param weightedOit 是否输出 OIT 累积与透射率附件。
 * @return 使用统一绑定约定的 GLSL 源码文本。
 */
inline std::string pbrFragment(bool weightedOit = false) {
    // 使用 GGX/Smith/Schlick 直接光照；法线贴图切线基由屏幕导数重建。
    // 阴影 PCF 对每个偏移样本修正接收平面深度，配合光栅偏移抑制自阴影条纹。
    // 共用光照代码，只替换输出阶段；OIT 使用加权预乘颜色和乘法透过率。
    const std::string output = weightedOit ? R"(
layout(location=0) out vec4 accumulation;
layout(location=1) out float revealage;
void writeColor(vec4 color){
    float alpha=clamp(color.a,0,1);
    float coverage=min(1.0,alpha*10.0)+0.01;
    float depthWeight=1.0-gl_FragCoord.z*0.9;
    float weight=clamp(coverage*coverage*coverage*1e4*depthWeight*depthWeight*depthWeight,0.01,3000.0);
    accumulation=vec4(color.rgb*alpha,alpha)*weight;
    revealage=alpha;
}
)"
                                           : R"(
layout(location=0) out vec4 outputColor;
void writeColor(vec4 color){outputColor=color;}
)";
    return std::string("#version 450\n") + frameBlock + materialBlock + output + R"(
/**
 * @brief 方向光、点光源或聚光灯组件；方向取节点局部 -Z。
 */
struct Light {vec4 positionType;vec4 directionRange;vec4 colorIntensity;vec4 coneShadow;};
layout(std430,set=0,binding=1) readonly buffer Lights {Light values[];} lights;
layout(set=0,binding=2) uniform sampler2DShadow shadowMap;
layout(set=1,binding=2) uniform sampler2D normalMap;
layout(set=1,binding=3) uniform sampler2D metallicRoughnessMap;
layout(set=1,binding=4) uniform sampler2D occlusionMap;
layout(set=1,binding=5) uniform sampler2D emissiveMap;
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 worldNormal;
layout(location=2) in vec2 uv;
layout(location=3) flat in vec4 instanceFlags;
layout(location=4) flat in vec4 instanceTint;
layout(location=5) flat in vec4 instanceOverlay;
layout(location=6) flat in vec4 instanceOutline;
layout(location=7) flat in vec4 instanceEffects;
layout(location=8) flat in vec4 instanceRect;
float spriteAlpha(vec2 p) {
    vec2 lo=min(instanceRect.xy,instanceRect.xy+instanceRect.zw);
    vec2 hi=max(instanceRect.xy,instanceRect.xy+instanceRect.zw);
    if(any(lessThan(p,lo))||any(greaterThan(p,hi)))return 0;
    vec2 halfTexel=0.5/vec2(textureSize(baseMap,0));
    return texture(baseMap,clamp(p,lo+halfTexel,hi-halfTexel)).a;
}

const float PI=3.14159265359;
vec3 safeNormalize(vec3 v){float len=dot(v,v);return len>1e-12?v*inversesqrt(len):vec3(0,1,0);}
vec3 normalValue(){
    vec3 n=safeNormalize(worldNormal);if(!gl_FrontFacing)n=-n;
    vec3 q1=dFdx(worldPosition),q2=dFdy(worldPosition);vec2 s1=dFdx(uv),s2=dFdy(uv);
    float determinant=s1.x*s2.y-s1.y*s2.x;
    if(abs(determinant)<1e-10)return n;
    vec3 t=(q1*s2.y-q2*s1.y)/determinant;t=safeNormalize(t-n*dot(n,t));
    vec3 b=safeNormalize(cross(n,t))*sign(determinant);
    vec3 mapped=texture(normalMap,uv).xyz*2-1;mapped.xy*=material.factors.w;
    return safeNormalize(mat3(t,b,n)*mapped);
}
float shadowValue(vec3 n,vec3 light){
    if(frame.shadowParameters.x<0.5||instanceFlags.x<0.5)return 1;
    vec4 clip=frame.lightViewProjection*vec4(worldPosition,1);vec3 p=clip.xyz/clip.w;p.xy=p.xy*0.5+0.5;
    vec3 dx=dFdx(p),dy=dFdy(p);float determinant=dx.x*dy.y-dx.y*dy.x;
    vec2 gradient=abs(determinant)>1e-10?vec2(dx.z*dy.y-dy.z*dx.y,dx.x*dy.z-dy.x*dx.z)/determinant:vec2(0);
    if(any(lessThan(p,vec3(0)))||any(greaterThan(p,vec3(1))))return 1;
    float bias=frame.shadowParameters.w*max(0.2,1-dot(n,light))+min(dot(abs(gradient),vec2(frame.shadowParameters.z*0.75)),0.01);float result=0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){vec2 offset=vec2(x,y)*frame.shadowParameters.z;result+=texture(shadowMap,vec3(p.xy+offset,p.z+dot(gradient,offset)-bias));}
    return result/9;
}
void main(){
    vec4 base=texture(baseMap,uv)*material.baseColor*instanceTint;
    if(material.misc.y>0.5&&material.misc.y<1.5&&base.a<material.factors.z)discard;
    if(instanceEffects.x>0.0&&instanceOutline.a>0.0) {
        vec2 pixel=vec2(instanceEffects.x)/vec2(textureSize(baseMap,0));
        float neighbor=1;
        for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)
            if(x!=0||y!=0)neighbor=min(neighbor,spriteAlpha(uv+vec2(x,y)*pixel));
        float edge=smoothstep(0.05,0.5,base.a)*(1.0-smoothstep(0.05,0.5,neighbor));
        base.rgb=mix(base.rgb,instanceOutline.rgb,edge*instanceOutline.a);
    }
    base.rgb=mix(base.rgb,instanceOverlay.rgb,instanceOverlay.a);
    vec3 emission=texture(emissiveMap,uv).rgb*material.emissive.rgb;
    if(material.misc.x>0.5){writeColor(vec4(base.rgb+emission,material.misc.y>1.5?base.a:1));return;}
    vec3 n=normalValue(),v=safeNormalize(frame.cameraPosition.xyz-worldPosition);
    vec4 mr=texture(metallicRoughnessMap,uv);float metallic=clamp(material.factors.x*mr.b,0,1),roughness=clamp(material.factors.y*mr.g,0.045,1);
    vec3 f0=mix(vec3(0.04),base.rgb,metallic),result=vec3(0);
    for(int i=0;i<int(frame.counts.x);++i){Light light=lights.values[i];vec3 l;float attenuation=1;
        if(light.positionType.w<0.5)l=-light.directionRange.xyz;
        else {vec3 delta=light.positionType.xyz-worldPosition;float distance=length(delta);l=safeNormalize(delta);float cutoff=clamp(1-pow(distance/light.directionRange.w,4),0,1);attenuation=cutoff*cutoff/max(distance*distance,0.01);if(light.positionType.w>1.5)attenuation*=smoothstep(light.coneShadow.y,light.coneShadow.x,dot(-l,light.directionRange.xyz));}
        vec3 h=safeNormalize(v+l);float nl=max(dot(n,l),0),nv=max(dot(n,v),0.001),nh=max(dot(n,h),0),hv=max(dot(h,v),0);
        float a=roughness*roughness,a2=a*a,d=nh*nh*(a2-1)+1;float distribution=a2/max(PI*d*d,1e-7);float k=(roughness+1)*(roughness+1)/8;
        float geometry=(nv/(nv*(1-k)+k))*(nl/(nl*(1-k)+k));vec3 fresnel=f0+(1-f0)*pow(1-hv,5);
        vec3 specular=distribution*geometry*fresnel/max(4*nv*nl,0.001);vec3 diffuse=(1-fresnel)*(1-metallic)*base.rgb/PI;
        float shadow=(i==int(frame.shadowParameters.y))?shadowValue(n,l):1;
        result+=(diffuse+specular)*light.colorIntensity.rgb*light.colorIntensity.a*attenuation*nl*shadow;
    }
    float ao=mix(1,texture(occlusionMap,uv).r,material.misc.z);
    result+=(base.rgb*(1-metallic)+f0*0.1)*frame.ambient.x*ao+emission;
    writeColor(vec4(result,material.misc.y>1.5?base.a:1));
}
)";
}
/**
 * @brief 生成全屏三角形顶点着色器源码。
 * @return 使用统一绑定约定的 GLSL 源码文本。
 */
inline std::string postVertex() {
    return R"(#version 450
layout(location=0) out vec2 uv;
void main(){vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);uv=p;gl_Position=vec4(p*2-1,0,1);}
)";
}
/**
 * @brief 生成曝光、ToneMap 与输出编码片元着色器源码。
 * @param encodedOutput 是否手工编码 sRGB 后写入普通 UNORM，供后续 FXAA 感知空间过滤。
 * @return 使用统一绑定约定的 GLSL 源码文本。
 */
inline std::string postFragment(bool encodedOutput = false) {
    return std::string("#version 450\n#define ENCODE_OUTPUT ") + (encodedOutput ? "1\n" : "0\n") +
           frameBlock + R"(
layout(set=2,binding=0) uniform sampler2D hdrImage;
layout(set=2,binding=1) uniform sampler2D sceneDepth;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 outputColor;
vec3 worldAt(vec2 p,float depth){vec4 h=frame.inverseViewProjection*vec4(p*2-1,depth,1);return h.xyz/h.w;}
float contactVisibility(){
    if(frame.contactParameters.x<=0)return 1;
    ivec2 extent=textureSize(sceneDepth,0);
    float d=texelFetch(sceneDepth,clamp(ivec2(uv*extent),ivec2(0),extent-1),0).r;
    if(d>=0.999999)return 1;
    vec3 origin=worldAt(uv,d), towardLight=normalize(-frame.lightDirection.xyz);
    int steps=int(frame.contactParameters.w);
    for(int i=1;i<=steps;++i){
        vec3 p=origin+towardLight*(frame.contactParameters.y*float(i)/float(steps));
        vec4 clip=frame.viewProjection*vec4(p,1);
        if(clip.w<=0)break;
        vec3 ndc=clip.xyz/clip.w;vec2 sampleUV=ndc.xy*0.5+0.5;
        if(any(lessThan(sampleUV,vec2(0)))||any(greaterThanEqual(sampleUV,vec2(1)))||ndc.z<0||ndc.z>1)break;
        float sampled=texelFetch(sceneDepth,clamp(ivec2(sampleUV*extent),ivec2(0),extent-1),0).r;
        if(sampled>=0.999999)continue;
        float gap=length(p-frame.cameraPosition.xyz)-length(worldAt(sampleUV,sampled)-frame.cameraPosition.xyz);
        if(ndc.z>sampled+0.00002&&gap>0.005&&gap<frame.contactParameters.z)
            return 1-frame.contactParameters.x*(1-float(i)/float(steps+1));
    }
    return 1;
}
void main(){vec3 color=texture(hdrImage,uv).rgb*frame.postParameters.x*contactVisibility();
    if(frame.postParameters.y>0.5&&frame.postParameters.y<1.5)color=clamp((color*(2.51*color+0.03))/(color*(2.43*color+0.59)+0.14),0,1);
    else if(frame.postParameters.y>1.5)color=color/(1+color);
    color=color*frame.colorMultiplySaturation.rgb+frame.colorAdd.rgb;
    color=mix(vec3(dot(color,vec3(0.2126,0.7152,0.0722))),color,frame.colorMultiplySaturation.a);
#if ENCODE_OUTPUT
    color=clamp(color,0,1);
    color=mix(12.92*color,1.055*pow(color,vec3(1.0/2.4))-0.055,
              greaterThan(color,vec3(0.0031308)));
#endif
    outputColor=vec4(color,1);
}
)";
}
/**
 * @brief 生成 FXAA 片元着色器，在编码后的 LDR 图像上识别边缘、搜索端点并执行亚像素过滤。
 * @return 读取普通 RGBA8 感知颜色、输出线性 RGB 到 sRGB 附件的 GLSL 源码。
 */
inline std::string fxaaFragment() {
    // 依据 FXAA 的局部对比、方向搜索及亚像素思路实现，固定搜索预算，不依赖历史帧。
    // 输入不是 sRGB 纹理：必须让双线性过滤在编码颜色上执行，避免自动解码为线性值。
    return R"(#version 450
layout(set=2,binding=0) uniform sampler2D colorImage;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 outputColor;
float luma(vec3 color){return dot(color,vec3(0.299,0.587,0.114));}
vec3 sampleColor(vec2 position){return textureLod(colorImage,position,0).rgb;}
float sampleLuma(vec2 position){return luma(sampleColor(position));}
void writeColor(vec3 color){
    // 最终附件为 sRGB，先解码回线性，交给附件编码；避免二次编码和非边缘亮度变化。
    color=mix(color/12.92,pow((color+0.055)/1.055,vec3(2.4)),
              greaterThan(color,vec3(0.04045)));
    outputColor=vec4(color,1);
}
void main(){
    vec2 pixel=1.0/vec2(textureSize(colorImage,0));
    vec3 center=sampleColor(uv);
    float m=luma(center);
    float n=sampleLuma(uv+vec2(0,pixel.y));
    float s=sampleLuma(uv-vec2(0,pixel.y));
    float e=sampleLuma(uv+vec2(pixel.x,0));
    float w=sampleLuma(uv-vec2(pixel.x,0));
    float low=min(m,min(min(n,s),min(e,w)));
    float high=max(m,max(max(n,s),max(e,w)));
    float contrast=high-low;
    if(contrast<max(1.0/32.0,high/8.0)){writeColor(center);return;}
    float nw=sampleLuma(uv+vec2(-pixel.x,pixel.y));
    float ne=sampleLuma(uv+pixel);
    float sw=sampleLuma(uv-pixel);
    float se=sampleLuma(uv+vec2(pixel.x,-pixel.y));
    float horizontal=abs(nw+sw-2*w)+2*abs(n+s-2*m)+abs(ne+se-2*e);
    float vertical=abs(nw+ne-2*n)+2*abs(w+e-2*m)+abs(sw+se-2*s);
    bool alongX=horizontal>=vertical;
    float positive=alongX?n:e, negative=alongX?s:w;
    bool towardPositive=abs(positive-m)>=abs(negative-m);
    float neighbor=towardPositive?positive:negative;
    vec2 normal=alongX?vec2(0,pixel.y):vec2(pixel.x,0);
    if(!towardPositive)normal=-normal;
    vec2 tangent=alongX?vec2(pixel.x,0):vec2(0,pixel.y);
    float pairMean=(m+neighbor)*0.5;
    float stopContrast=abs(neighbor-m)*0.25;
    vec2 origin=uv+normal*0.5;
    vec2 endNegative=origin-tangent,endPositive=origin+tangent;
    float differenceNegative=sampleLuma(endNegative)-pairMean;
    float differencePositive=sampleLuma(endPositive)-pairMean;
    bool foundNegative=abs(differenceNegative)>=stopContrast;
    bool foundPositive=abs(differencePositive)>=stopContrast;
    // 沿边缘每次前进一个像素，最多搜索 12 个像素，限制高对比复杂图案的成本。
    for(int step=1;step<12&&!(foundNegative&&foundPositive);++step){
        if(!foundNegative){
            endNegative-=tangent;
            differenceNegative=sampleLuma(endNegative)-pairMean;
            foundNegative=abs(differenceNegative)>=stopContrast;
        }
        if(!foundPositive){
            endPositive+=tangent;
            differencePositive=sampleLuma(endPositive)-pairMean;
            foundPositive=abs(differencePositive)>=stopContrast;
        }
    }
    float distanceNegative=alongX?uv.x-endNegative.x:uv.y-endNegative.y;
    float distancePositive=alongX?endPositive.x-uv.x:endPositive.y-uv.y;
    bool nearerNegative=distanceNegative<=distancePositive;
    float endDifference=nearerNegative?differenceNegative:differencePositive;
    bool foundEnd=nearerNegative?foundNegative:foundPositive;
    float edgeOffset=0;
    if(foundEnd&&((endDifference<0)!=(m<pairMean)))
        edgeOffset=0.5-min(distanceNegative,distancePositive)/
                       max(distanceNegative+distancePositive,1e-8);
    float average=((n+s+e+w)*2+nw+ne+sw+se)/12;
    float subpixel=clamp(abs(average-m)/max(contrast,1e-8),0,1);
    subpixel=subpixel*subpixel*(3-2*subpixel);
    subpixel=subpixel*subpixel*0.75;
    writeColor(sampleColor(uv+normal*max(edgeOffset,subpixel)));
}
)";
}
/**
 * @brief 生成加权 OIT 合成片元着色器源码。
 * @return 使用统一绑定约定的 GLSL 源码文本。
 */
inline std::string oitResolveFragment() {
    // 读取累积目标，按总覆盖率混合到已存在的 HDR；没有透明片元时不改动背景。
    return R"(#version 450
layout(set=2,binding=0) uniform sampler2D accumulationImage;
layout(set=2,binding=1) uniform sampler2D revealageImage;
layout(location=0) out vec4 outputColor;
void main(){
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    float revealage=clamp(texelFetch(revealageImage,pixel,0).r,0,1);
    if(revealage>=1)discard;
    vec4 accumulation=texelFetch(accumulationImage,pixel,0);
    outputColor=vec4(accumulation.rgb/max(accumulation.a,1e-8),1-revealage);
}
)";
}
} // namespace renderer::detail

#endif // RENDERER_SRC_BACKENDS_COMMON_PORTABLE_SHADERS_HPP_INCLUDED
