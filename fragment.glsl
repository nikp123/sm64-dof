#version 100

precision highp float;
precision highp int;

varying vec2 texCoord;

uniform sampler2D color_texture;
uniform sampler2D depth_texture;

vec4 blur9(sampler2D image, vec2 uv, vec2 resolution, vec2 direction) {
  vec4 color = vec4(0.0);
  vec2 off1 = vec2(1.3846153846) * direction;
  vec2 off2 = vec2(3.2307692308) * direction;
  color += texture2D(image, uv) * 0.2270270270;
  color += texture2D(image, uv + (off1 / resolution)) * 0.3162162162;
  color += texture2D(image, uv - (off1 / resolution)) * 0.3162162162;
  color += texture2D(image, uv + (off2 / resolution)) * 0.0702702703;
  color += texture2D(image, uv - (off2 / resolution)) * 0.0702702703;
  return color;
}

float get_distance(sampler2D tex, vec2 coord) {
   float depth = texture2D(tex, coord).r;
   float Znear = 0.1;
   float Zfar  = 10000.0;

   return Znear*Zfar / (Zfar - depth*(Zfar-Znear));
}

void main() {
   float strenght = abs(get_distance(depth_texture, texCoord) - 
                    get_distance(depth_texture, vec2(0.0, 0.0)));

   strenght /= 10.0;

   strenght = min(strenght, 1.0);

   gl_FragColor = blur9(color_texture, 
         texCoord,
         vec2(800, 600),
         vec2(1, 1)*strenght);
}
