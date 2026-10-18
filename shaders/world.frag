#version 450
layout(binding=0) uniform sampler2D atlas;
layout(location=0) in vec2 uv;
layout(location=1) in vec3 tint;
layout(location=2) in float fog;
layout(location=3) flat in int material;
layout(location=4) in vec3 worldPosition;
layout(location=5) in vec3 fogColor;
layout(location=6) in vec4 lavaLight;
layout(location=0) out vec4 outputColor;
void main() {
 // Surface grass/sand/snow have no underside; caverns supply their own rock ceiling.
 if((material==100 || material==104) && gl_FrontFacing) discard;
 vec4 t;
 if(material==204) {
  ivec2 pixel=ivec2(fract(worldPosition.xz*0.5)*16.0);
  t=texelFetch(atlas,ivec2(25*16+pixel.x,pixel.y),0);
  t.rgb=max(t.rgb,vec3(0.8,0.15,0.01));t.a=1.0;
 } else if(material==201) t=vec4(0.96,0.98,1.0,0.9);
 else if(material==202) t=vec4(0.55,0.68,0.87,0.55);
 else if(material==203) t=vec4(1.0,1.0,1.0,1.0);
 else if(material==200) {
  t=texelFetch(atlas,ivec2(clamp(uv,vec2(0),vec2(0.99999))*64.0)+ivec2(0,128),0);
  vec3 n=normalize(cross(dFdx(worldPosition),dFdy(worldPosition)));if(!gl_FrontFacing)n=-n;
  float light=min(1.0,(max(0.0,dot(normalize(vec3(0.2,1.0,-0.7)),n))+max(0.0,dot(normalize(vec3(-0.2,1.0,0.7)),n)))*0.6+0.4);
  t.rgb*=light;
 } else if(material>=100) {
  vec3 n=normalize(cross(dFdx(worldPosition),dFdy(worldPosition))); if(n.y<0) n=-n;
  int topTile=material-100;
  int rockTile=worldPosition.y<0.0?26:3;
  if(topTile==3)topTile=rockTile;
  vec3 axis=abs(n);
  bool topFace=axis.y>=axis.x && axis.y>=axis.z;
  vec2 projected=topFace?worldPosition.xz:axis.x>axis.z?worldPosition.zy:worldPosition.xy;
  int tile=topFace?topTile:rockTile;
  ivec2 pixel=ivec2(fract(projected*0.5)*16.0);
  t=texelFetch(atlas,ivec2(tile*16+pixel.x,pixel.y),0);
  vec4 top=t;
  if(topTile==5 || topTile==7) {ivec2 p=ivec2(fract(worldPosition.xz*0.5)*16.0);t=texelFetch(atlas,ivec2(topTile*16+p.x,p.y),0);}
  float light=dot(abs(n),vec3(0.6,1.0,0.8))/max(dot(abs(n),vec3(1)),0.001);
  t.rgb*=light;
 } else if(material>=0) {
  ivec2 pixel=ivec2(fract(uv)*16.0);
  t=texelFetch(atlas,ivec2(material*16+pixel.x,pixel.y),0);
 } else if(material==-2) t=texelFetch(atlas,ivec2(uv*512.0),0);
 else t=texture(atlas,uv);
 if(material==8) {
  t.rgb=vec3(0.08,0.68,0.86)*(0.94+0.06*t.r);
  t.a=0.42;
 }
 if(t.a<0.1) discard;
 vec3 lit=t.rgb*tint;
 if(material>=0 && material!=204 && lavaLight.w>0.0) {
  float glow=pow(max(0.0,1.0-length(worldPosition-lavaLight.xyz)/lavaLight.w),2.0);
  lit+=t.rgb*vec3(1.6,0.55,0.08)*glow;
 }
 if(material==204)lit=t.rgb;
 outputColor=vec4(mix(lit,fogColor,material==204?fog*0.3:fog),t.a);
}
