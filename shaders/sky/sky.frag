#version 460 core
// Sky shader with volumetric clouds, stars, and moon
// Supports both simple Minecraft-style clouds and ray-marched volumetric clouds

layout(location = 0) in vec2 screenPos;
layout(location = 0) out vec4 FragColor;

uniform mat4 invView;
uniform mat4 invProjection;
uniform vec3 cameraPos;
uniform vec3 sunDirection;
uniform vec3 skyColorTop;
uniform vec3 skyColorBottom;
uniform float time;
uniform int cloudStyle;  // 0 = simple, 1 = volumetric
uniform float cloudRenderDistance;  // Limit cloud rendering to render distance

// Simple cloud settings (3D rounded shapes)
const float SIMPLE_CLOUD_MIN = 110.0;
const float SIMPLE_CLOUD_MAX = 160.0;     // 50 block thickness for puffy clouds
const float SIMPLE_CLOUD_THICKNESS = 50.0;
const int SIMPLE_CLOUD_STEPS = 12;        // More steps for better 3D shapes
const float SIMPLE_CLOUD_SCALE = 0.012;   // Scale for cloud size

// Volumetric cloud settings
const float CLOUD_MIN = 100.0;
const float CLOUD_MAX = 220.0;
const float CLOUD_THICKNESS = 120.0;
const int CLOUD_STEPS = 40;
const int LIGHT_STEPS = 5;
const float CLOUD_DENSITY = 0.25;
const float CLOUD_COVERAGE = 0.35;
const float ABSORPTION = 0.45;
const float SCATTERING_FORWARD = 0.75;
const float SCATTERING_BACK = 0.25;
const float AMBIENT_STRENGTH = 0.6;
const float CLOUD_SCALE = 0.003;

// ============================================================
// 2D Simplex Noise (for simple clouds)
// ============================================================
vec3 permute(vec3 x) { return mod(((x*34.0)+1.0)*x, 289.0); }

float snoise2D(vec2 v) {
    const vec4 C = vec4(0.211324865405187, 0.366025403784439,
                        -0.577350269189626, 0.024390243902439);
    vec2 i = floor(v + dot(v, C.yy));
    vec2 x0 = v - i + dot(i, C.xx);
    vec2 i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec4 x12 = x0.xyxy + C.xxzz;
    x12.xy -= i1;
    i = mod(i, 289.0);
    vec3 p = permute(permute(i.y + vec3(0.0, i1.y, 1.0)) + i.x + vec3(0.0, i1.x, 1.0));
    vec3 m = max(0.5 - vec3(dot(x0,x0), dot(x12.xy,x12.xy), dot(x12.zw,x12.zw)), 0.0);
    m = m*m;
    m = m*m;
    vec3 x = 2.0 * fract(p * C.www) - 1.0;
    vec3 h = abs(x) - 0.5;
    vec3 ox = floor(x + 0.5);
    vec3 a0 = x - ox;
    m *= 1.79284291400159 - 0.85373472095314 * (a0*a0 + h*h);
    vec3 g;
    g.x = a0.x * x0.x + h.x * x0.y;
    g.yz = a0.yz * x12.xz + h.yz * x12.yw;
    return 130.0 * dot(m, g);
}

// ============================================================
// 3D Simplex Noise (for volumetric clouds)
// ============================================================
vec4 permute4(vec4 x) { return mod(((x*34.0)+1.0)*x, 289.0); }
vec4 taylorInvSqrt(vec4 r) { return 1.79284291400159 - 0.85373472095314 * r; }

float snoise3D(vec3 v) {
    const vec2 C = vec2(1.0/6.0, 1.0/3.0);
    const vec4 D = vec4(0.0, 0.5, 1.0, 2.0);

    vec3 i = floor(v + dot(v, C.yyy));
    vec3 x0 = v - i + dot(i, C.xxx);

    vec3 g = step(x0.yzx, x0.xyz);
    vec3 l = 1.0 - g;
    vec3 i1 = min(g.xyz, l.zxy);
    vec3 i2 = max(g.xyz, l.zxy);

    vec3 x1 = x0 - i1 + C.xxx;
    vec3 x2 = x0 - i2 + C.yyy;
    vec3 x3 = x0 - D.yyy;

    i = mod(i, 289.0);
    vec4 p = permute4(permute4(permute4(
        i.z + vec4(0.0, i1.z, i2.z, 1.0))
        + i.y + vec4(0.0, i1.y, i2.y, 1.0))
        + i.x + vec4(0.0, i1.x, i2.x, 1.0));

    float n_ = 1.0/7.0;
    vec3 ns = n_ * D.wyz - D.xzx;
    vec4 j = p - 49.0 * floor(p * ns.z * ns.z);
    vec4 x_ = floor(j * ns.z);
    vec4 y_ = floor(j - 7.0 * x_);
    vec4 x = x_ * ns.x + ns.yyyy;
    vec4 y = y_ * ns.x + ns.yyyy;
    vec4 h = 1.0 - abs(x) - abs(y);
    vec4 b0 = vec4(x.xy, y.xy);
    vec4 b1 = vec4(x.zw, y.zw);
    vec4 s0 = floor(b0)*2.0 + 1.0;
    vec4 s1 = floor(b1)*2.0 + 1.0;
    vec4 sh = -step(h, vec4(0.0));
    vec4 a0 = b0.xzyw + s0.xzyw*sh.xxyy;
    vec4 a1 = b1.xzyw + s1.xzyw*sh.zzww;
    vec3 p0 = vec3(a0.xy, h.x);
    vec3 p1 = vec3(a0.zw, h.y);
    vec3 p2 = vec3(a1.xy, h.z);
    vec3 p3 = vec3(a1.zw, h.w);
    vec4 norm = taylorInvSqrt(vec4(dot(p0,p0), dot(p1,p1), dot(p2,p2), dot(p3,p3)));
    p0 *= norm.x; p1 *= norm.y; p2 *= norm.z; p3 *= norm.w;
    vec4 m = max(0.6 - vec4(dot(x0,x0), dot(x1,x1), dot(x2,x2), dot(x3,x3)), 0.0);
    m = m * m;
    return 42.0 * dot(m*m, vec4(dot(p0,x0), dot(p1,x1), dot(p2,x2), dot(p3,x3)));
}

// ============================================================
// Simple Minecraft-style clouds (3D rounded puffy shapes)
// ============================================================
float getSimpleCloudDensity3D(vec3 pos) {
    // Slow wind animation
    vec3 windOffset = vec3(time * 2.0, 0.0, time * 0.8);

    // UNIFORM 3D scaling - this is key for rounded shapes
    vec3 samplePos = (pos + windOffset) * SIMPLE_CLOUD_SCALE;

    // Multi-octave 3D noise for puffy, rounded shapes
    float n1 = snoise3D(samplePos) * 0.5;
    float n2 = snoise3D(samplePos * 2.02 + vec3(50.0, 30.0, 80.0)) * 0.25;
    float n3 = snoise3D(samplePos * 4.01 + vec3(100.0, 60.0, 40.0)) * 0.125;
    float n4 = snoise3D(samplePos * 8.03 + vec3(25.0, 90.0, 120.0)) * 0.0625;

    float noise = n1 + n2 + n3 + n4;

    // Height profile for puffy cumulus shape
    float heightNorm = (pos.y - SIMPLE_CLOUD_MIN) / SIMPLE_CLOUD_THICKNESS;

    // Sharp flat bottom, gradual rounded top
    float bottomCutoff = smoothstep(0.0, 0.1, heightNorm);
    float topRoundoff = 1.0 - pow(max(heightNorm - 0.3, 0.0) / 0.7, 2.0);
    topRoundoff = max(topRoundoff, 0.0);

    float heightProfile = bottomCutoff * topRoundoff;

    // Cloud coverage threshold
    float baseThreshold = 0.1;
    float threshold = baseThreshold + heightNorm * 0.15;

    float density = smoothstep(threshold, threshold + 0.2, noise) * heightProfile;
    density = pow(density, 0.8) * 1.2;

    return clamp(density, 0.0, 1.0);
}

vec4 renderSimpleClouds(vec3 rayDir) {
    float tMin, tMax;

    if (abs(rayDir.y) < 0.001) {
        if (cameraPos.y < SIMPLE_CLOUD_MIN || cameraPos.y > SIMPLE_CLOUD_MAX) {
            return vec4(0.0);
        }
        tMin = 0.0;
        tMax = 3000.0;
    } else {
        float t1 = (SIMPLE_CLOUD_MIN - cameraPos.y) / rayDir.y;
        float t2 = (SIMPLE_CLOUD_MAX - cameraPos.y) / rayDir.y;
        tMin = min(t1, t2);
        tMax = max(t1, t2);

        if (cameraPos.y >= SIMPLE_CLOUD_MIN && cameraPos.y <= SIMPLE_CLOUD_MAX) {
            tMin = 0.0;
        }
        tMin = max(tMin, 0.0);
        tMax = max(tMax, 0.0);
    }

    if (tMax <= tMin) return vec4(0.0);

    float maxCloudDist = cloudRenderDistance * 16.0;
    if (tMin > maxCloudDist) return vec4(0.0);
    tMax = min(tMax, min(tMin + 400.0, maxCloudDist));

    float stepSize = (tMax - tMin) / float(SIMPLE_CLOUD_STEPS);

    float jitter = fract(sin(dot(screenPos, vec2(12.9898, 78.233))) * 43758.5453);
    float t = tMin + stepSize * jitter * 0.5;

    float transmittance = 1.0;
    vec3 lightAccum = vec3(0.0);

    vec3 cloudBright = vec3(1.0, 1.0, 1.0);
    vec3 cloudShadow = vec3(0.75, 0.8, 0.9);

    for (int i = 0; i < SIMPLE_CLOUD_STEPS; i++) {
        vec3 pos = cameraPos + rayDir * t;
        float density = getSimpleCloudDensity3D(pos);

        if (density > 0.01) {
            vec3 lightSamplePos = pos + sunDirection * 8.0;
            float lightDensity = getSimpleCloudDensity3D(lightSamplePos);
            float shadowAmount = exp(-lightDensity * 2.0);

            float heightNorm = (pos.y - SIMPLE_CLOUD_MIN) / SIMPLE_CLOUD_THICKNESS;
            float heightLight = 0.5 + 0.5 * heightNorm;

            float totalLight = shadowAmount * 0.7 + heightLight * 0.3;
            float sunUp = max(sunDirection.y, 0.0);
            totalLight *= 0.7 + 0.3 * sunUp;

            vec3 cloudColor = mix(cloudShadow, cloudBright, totalLight);

            float absorption = exp(-density * stepSize * 3.0);
            float alpha = 1.0 - absorption;

            lightAccum += transmittance * cloudColor * alpha;
            transmittance *= absorption;

            if (transmittance < 0.02) break;
        }

        t += stepSize;
    }

    float distFade = 1.0 - smoothstep(1500.0, 2500.0, tMin);
    float finalAlpha = (1.0 - transmittance) * distFade;
    vec3 finalColor = lightAccum / max(1.0 - transmittance, 0.001);

    return vec4(finalColor, finalAlpha);
}

// ============================================================
// Volumetric cloud functions
// ============================================================
float fbmClouds(vec3 p) {
    float value = 0.0;
    float amplitude = 0.55;
    float frequency = 1.0;
    mat3 rot = mat3(0.80, 0.60, 0.00, -0.60, 0.80, 0.00, 0.00, 0.00, 1.00);
    for (int i = 0; i < 6; i++) {
        value += amplitude * snoise3D(p * frequency);
        p = rot * p;
        frequency *= 1.95;
        amplitude *= 0.55;
    }
    return value;
}

float getVolCloudDensity(vec3 p) {
    vec3 windOffset = vec3(time * 1.2, 0.0, time * 0.5);
    vec3 samplePos = (p + windOffset) * CLOUD_SCALE;
    float density = fbmClouds(samplePos);
    float heightFactor = (p.y - CLOUD_MIN) / CLOUD_THICKNESS;
    float bottomFalloff = smoothstep(0.0, 0.15, heightFactor);
    float topFalloff = smoothstep(1.0, 0.4, heightFactor);
    float cumulusProfile = pow(bottomFalloff * topFalloff, 0.7);
    density = (density - CLOUD_COVERAGE) * cumulusProfile;
    density = max(density, 0.0) * CLOUD_DENSITY;
    return pow(max(density, 0.0), 0.85);
}

vec2 rayBoxIntersect(vec3 ro, vec3 rd, float minY, float maxY) {
    float tMin = (minY - ro.y) / rd.y;
    float tMax = (maxY - ro.y) / rd.y;
    if (tMin > tMax) { float temp = tMin; tMin = tMax; tMax = temp; }
    return vec2(max(tMin, 0.0), max(tMax, 0.0));
}

float henyeyGreenstein(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159 * pow(1.0 + g2 - 2.0*g*cosTheta, 1.5));
}

float cloudPhase(float cosTheta) {
    return mix(henyeyGreenstein(cosTheta, -SCATTERING_BACK),
               henyeyGreenstein(cosTheta, SCATTERING_FORWARD), 0.7);
}

float lightMarch(vec3 pos) {
    float totalDensity = 0.0;
    float stepSize = CLOUD_THICKNESS / float(LIGHT_STEPS);
    for (int i = 0; i < LIGHT_STEPS; i++) {
        pos += sunDirection * stepSize;
        if (pos.y > CLOUD_MAX || pos.y < CLOUD_MIN) break;
        totalDensity += getVolCloudDensity(pos) * stepSize;
    }
    return exp(-totalDensity * ABSORPTION);
}

vec4 renderVolumetricClouds(vec3 rayDir) {
    if (rayDir.y <= -0.1) return vec4(0.0);

    vec2 tCloud = rayBoxIntersect(cameraPos, rayDir, CLOUD_MIN, CLOUD_MAX);
    if (tCloud.y <= tCloud.x) return vec4(0.0);

    float maxCloudDist = cloudRenderDistance * 16.0;
    if (tCloud.x > maxCloudDist) return vec4(0.0);

    float tStart = tCloud.x;
    float tEnd = min(tCloud.y, min(tCloud.x + 500.0, maxCloudDist));

    float distanceFactor = clamp(tStart / 500.0, 0.0, 1.0);
    int adaptiveSteps = int(mix(float(CLOUD_STEPS), float(CLOUD_STEPS / 2), distanceFactor));
    float stepSize = (tEnd - tStart) / float(adaptiveSteps);

    float blueNoise = fract(sin(dot(screenPos, vec2(12.9898, 78.233))) * 43758.5453);
    float t = tStart + stepSize * blueNoise;

    float transmittance = 1.0;
    vec3 lightEnergy = vec3(0.0);
    float cosTheta = dot(rayDir, sunDirection);
    float phase = cloudPhase(cosTheta);

    vec3 sunLight = vec3(1.0, 0.98, 0.9);
    vec3 ambientLight = skyColorTop * 0.8;
    vec3 cloudBase = vec3(1.0);
    vec3 cloudShadow = vec3(0.7, 0.75, 0.85);

    for (int i = 0; i < CLOUD_STEPS; i++) {
        if (transmittance < 0.03) break;
        if (i >= adaptiveSteps) break;

        vec3 pos = cameraPos + rayDir * t;
        float density = getVolCloudDensity(pos);

        if (density > 0.001) {
            float lightTransmittance = lightMarch(pos);
            float heightGrad = clamp((pos.y - CLOUD_MIN) / CLOUD_THICKNESS, 0.0, 1.0);
            vec3 directLight = sunLight * lightTransmittance * phase * 2.0;
            vec3 ambient = ambientLight * AMBIENT_STRENGTH * (0.5 + 0.5 * heightGrad);
            vec3 cloudCol = mix(cloudShadow, cloudBase, lightTransmittance);
            cloudCol += vec3(1.0, 0.95, 0.9) * pow(max(cosTheta, 0.0), 2.0) * (1.0 - lightTransmittance) * 0.5;
            vec3 sampleColor = cloudCol * (directLight + ambient);
            float beers = exp(-density * stepSize * ABSORPTION);
            float powder = 1.0 - exp(-density * stepSize * 2.0);
            float sampleTransmit = mix(beers, beers * powder, 0.5);
            lightEnergy += transmittance * sampleColor * density * stepSize;
            transmittance *= sampleTransmit;
        }
        t += stepSize;
    }
    return vec4(lightEnergy, 1.0 - transmittance);
}

// ============================================================
// Star field generation
// ============================================================
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

vec3 renderStars(vec3 rayDir) {
    if (rayDir.y < 0.0) return vec3(0.0);

    vec3 stars = vec3(0.0);
    vec3 starDir = normalize(rayDir);

    float phi = atan(starDir.z, starDir.x);
    float theta = acos(starDir.y);

    for (int layer = 0; layer < 2; layer++) {
        float scale = 60.0 + float(layer) * 30.0;
        vec2 starUV = vec2(phi, theta) * scale;
        vec2 cell = floor(starUV);

        for (int x = -1; x <= 1; x++) {
            for (int y = -1; y <= 1; y++) {
                vec2 neighbor = cell + vec2(x, y);
                vec2 cellHash = neighbor + float(layer) * 100.0;

                float h = hash(cellHash);
                if (h > 0.96) {
                    vec2 starCenter = neighbor + vec2(
                        hash(cellHash + vec2(1.0, 0.0)),
                        hash(cellHash + vec2(0.0, 1.0))
                    );

                    float dist = length(starUV - starCenter);
                    float starSize = 0.12 + hash(cellHash + vec2(5.0)) * 0.18;

                    if (dist < starSize) {
                        float twinkle = sin(time * (2.0 + h * 4.0) + h * 6.28) * 0.3 + 0.7;
                        float brightness = (1.0 - dist / starSize) * twinkle;
                        brightness = brightness * brightness;

                        float colorHash = hash(cellHash + vec2(10.0));
                        vec3 starColor = vec3(1.0);
                        if (colorHash > 0.85) starColor = vec3(1.0, 0.8, 0.6);
                        else if (colorHash > 0.7) starColor = vec3(0.8, 0.9, 1.0);

                        stars += starColor * brightness * 0.9;
                    }
                }
            }
        }
    }

    return stars;
}

void main() {
    // Reconstruct ray direction
    vec4 clipPos = vec4(screenPos, 1.0, 1.0);
    vec4 viewPos = invProjection * clipPos;
    viewPos = vec4(viewPos.xy, -1.0, 0.0);
    vec3 rayDir = normalize((invView * viewPos).xyz);

    // Sky gradient
    float skyGradient = clamp(rayDir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 sky = mix(skyColorBottom, skyColorTop, pow(skyGradient, 0.7));

    // Sun
    float sunDot = dot(rayDir, sunDirection);
    float sunDisc = smoothstep(0.9985, 0.9995, sunDot);
    vec3 sunColor = vec3(1.0, 0.95, 0.8) * 2.0;
    sky += vec3(1.0, 0.8, 0.5) * pow(max(sunDot, 0.0), 8.0) * 0.3;

    // Stars (only visible at night)
    float nightFactor = 1.0 - smoothstep(-0.1, 0.2, sunDirection.y);
    if (nightFactor > 0.01) {
        vec3 stars = renderStars(rayDir);
        sky += stars * nightFactor;
    }

    // Moon (opposite side of sun)
    vec3 moonDir = -sunDirection;
    float moonDot = dot(rayDir, moonDir);
    float moonDisc = smoothstep(0.998, 0.9995, moonDot);
    vec3 moonColor = vec3(0.9, 0.9, 1.0) * 0.8;
    sky += moonDisc * moonColor * nightFactor;

    // Render clouds based on style (-1 = disabled, 0 = simple, 1 = volumetric)
    vec4 cloudColor = vec4(0.0);
    if (cloudStyle == 0) {
        cloudColor = renderSimpleClouds(rayDir);
    } else if (cloudStyle == 1) {
        cloudColor = renderVolumetricClouds(rayDir);
    }

    // Composite
    vec3 finalColor = mix(sky, cloudColor.rgb, cloudColor.a);
    finalColor += sunDisc * sunColor * (1.0 - cloudColor.a * 0.8);

    FragColor = vec4(finalColor, 1.0);
}
