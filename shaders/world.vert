#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec2 texcoord;
layout(location=2) in vec3 color;
layout(location=3) in float hud;
layout(push_constant) uniform Camera { mat4 vp; vec4 eye; vec4 environment; vec4 lavaLight; vec4 subsurface; } camera;
layout(location=0) out vec2 uv;
layout(location=1) out vec3 tint;
layout(location=2) out float fog;
layout(location=3) flat out int material;
layout(location=4) out vec3 worldPosition;
layout(location=5) out vec3 fogColor;
layout(location=6) out vec4 lavaLight;
void main() {
 gl_Position = hud > 0.5 ? vec4(position,1) : camera.vp*vec4(position,1);
 lavaLight=camera.lavaLight;
 uv=texcoord; tint=color; worldPosition=position;
 material=hud>0.5?(hud>1.5?int(hud-2.0):hud>1.1?-2:-1):int(-hud-1.0);
 fog=hud>0.5?0.0:clamp((max(length((position-camera.eye.xyz).xz),abs(position.y-camera.eye.y))-camera.eye.w*0.75)/(camera.eye.w*0.25),0.0,1.0);
 fogColor=camera.environment.x>0.5?vec3(0.02,0.18,0.32):vec3(0.58,0.76,0.9);
 if(hud<0.5 && camera.environment.x<0.5) {
  float clouds=camera.environment.y, flash=camera.environment.z;
  tint*=mix(vec3(1),vec3(0.64,0.68,0.74),clouds);
  tint=mix(tint,vec3(1.2),flash*0.7);
  fogColor=mix(fogColor,vec3(0.38,0.42,0.48),clouds);
  fogColor=mix(fogColor,vec3(0.88,0.91,1),flash);
  if(camera.environment.w>0.5) fog=max(fog,clamp(length(position-camera.eye.xyz)/max(128.0,camera.eye.w*.6),0.0,1.0)*clouds*.75);
 }
 if(camera.subsurface.x>0.5 && camera.environment.x<0.5) {fogColor=vec3(0.055,0.065,0.075);}
 if(material==203) tint=color;
 if(hud<0.5 && camera.environment.x>0.5) fog=max(fog,clamp(length(position-camera.eye.xyz)/96.0,0.0,1.0));
}
