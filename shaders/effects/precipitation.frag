#version 460 core
in float vAlpha;
in float vSize;
in vec2 vScreenPos;

out vec4 FragColor;

uniform int weatherType;  // 1 = rain, 2 = snow, 3 = thunderstorm
uniform float intensity;
uniform vec3 lightColor;

void main() {
    vec2 coord = gl_PointCoord * 2.0 - 1.0;

    if (weatherType == 2) {
        // Snow - soft circular flakes
        float dist = length(coord);
        float alpha = 1.0 - smoothstep(0.3, 1.0, dist);

        // Subtle sparkle
        float sparkle = max(0.0, sin(coord.x * 10.0) * sin(coord.y * 10.0)) * 0.3;

        vec3 snowColor = vec3(0.95, 0.97, 1.0) + sparkle;
        FragColor = vec4(snowColor * lightColor, alpha * vAlpha * intensity * 0.8);
    } else {
        // Rain - elongated streaks
        float rainShape = abs(coord.x) * 4.0 + abs(coord.y - 0.3) * 0.5;
        float alpha = 1.0 - smoothstep(0.0, 1.0, rainShape);

        // Slight blue tint for rain
        vec3 rainColor = vec3(0.7, 0.8, 0.95);
        FragColor = vec4(rainColor * lightColor, alpha * vAlpha * intensity * 0.6);
    }

    if (FragColor.a < 0.01) discard;
}
