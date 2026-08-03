#version 450

layout (location = 0) in vec2 inTexCoord;

layout (set = 0, binding = 0) uniform sampler2D floatColor2;

// Post-processing options uniform buffer
layout (set = 0, binding = 1) uniform PostProcessingOptions {
    // Tone mapping options
    int toneMappingType;        // 0=None, 1=Reinhard, 2=ACES, 3=Uncharted2, 4=GT, 5=Lottes, 6=Exponential, 7=ReinhardExtended, 8=Luminance, 9=Hable
    float exposure;             // HDR exposure adjustment
    float gamma;                // Gamma correction value
    float maxWhite;             // For extended Reinhard tone mapping
    
    // Color grading
    float contrast;             // Contrast adjustment
    float brightness;           // Brightness adjustment  
    float saturation;           // Color saturation
    float vibrance;             // Vibrance (smart saturation)
    
    // Effects
    float vignetteStrength;     // Vignette effect strength
    float vignetteRadius;       // Vignette radius
    float filmGrainStrength;    // Film grain noise strength
    float chromaticAberration;  // 0.0-1.0: Chromatic aberration, >1.0: FXAA strength
    
    // Debug and visualization
    int debugMode;              // 0=Off, 1=Show tone mapping comparison, 2=Show color channels
    float debugSplit;           // Split position for comparison (0.0-1.0)
    int showOnlyChannel;        // 0=All, 1=Red, 2=Green, 3=Blue, 4=Alpha, 5=Luminance
    float padding1;             // Bokeh DOF parameters: focusDistance + aperture + bokehIntensity encoded

    // NIS-style spatial upscaling controls. Kept in this UBO to avoid another descriptor set.
    int nisEnabled;
    float nisSharpness;
    float nisScaleThreshold;
    float nisPadding;
} postOptions;

// Add depth buffer access for Bokeh effect
layout (set = 0, binding = 2) uniform sampler2D depthStencil;
layout (set = 0, binding = 3) uniform sampler2D temporalOutput;

layout (location = 0) out vec4 outFragColor;

// ===== TONE MAPPING FUNCTIONS =====

vec3 reinhardToneMapping(vec3 color) {
    return color / (color + vec3(1.0));
}

vec3 acesToneMapping(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

vec3 uncharted2ToneMapping(vec3 color) {
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    const float W = 11.2;
    
    vec3 curr = ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;
    vec3 whiteScale = ((vec3(W) * (A * vec3(W) + C * B) + D * E) / (vec3(W) * (A * vec3(W) + B) + D * F)) - E / F;
    return curr / whiteScale;
}

vec3 gtToneMapping(vec3 color) {
    const float P = 1.0;
    const float a = 1.0;
    const float m = 0.22;
    const float l = 0.4;
    const float c = 1.33;
    const float b = 0.0;
    
    float l0 = ((P - m) * l) / a;
    float L0 = m - m / a;
    float L1 = m + (1.0 - m) / a;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float C2 = (a * P) / (P - S1);
    float CP = -C2 / P;

    vec3 w0 = vec3(1.0) - smoothstep(0.0, m, color);
    vec3 w2 = step(m + l0, color);
    vec3 w1 = vec3(1.0) - w0 - w2;

    vec3 T = m * pow(color / m, vec3(c)) + b;
    vec3 S = P - (P - S1) * exp(CP * (color - S0));
    vec3 L = m + a * (color - m);

    return T * w0 + L * w1 + S * w2;
}

vec3 lottesToneMapping(vec3 color) {
    const vec3 a = vec3(1.6);
    const vec3 d = vec3(0.977);
    const vec3 hdrMax = vec3(8.0);
    const vec3 midIn = vec3(0.18);
    const vec3 midOut = vec3(0.267);
    
    const vec3 b = (-pow(midIn, a) + pow(hdrMax, a) * midOut) / 
                   ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);
    const vec3 c = (pow(hdrMax, a * d) * pow(midIn, a) - pow(hdrMax, a) * pow(midIn, a * d) * midOut) / 
                   ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);
    
    return pow(color, a) / (pow(color, a * d) * b + c);
}

vec3 exponentialToneMapping(vec3 color) {
    return vec3(1.0) - exp(-color * 1.0);
}

vec3 reinhardExtendedToneMapping(vec3 color, float maxWhite) {
    vec3 numerator = color * (1.0 + (color / (maxWhite * maxWhite)));
    return numerator / (1.0 + color);
}

vec3 luminanceToneMapping(vec3 color) {
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return color / (1.0 + luminance);
}

vec3 hable(vec3 color) {
    const float A = 0.22;
    const float B = 0.30;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.01;
    const float F = 0.30;
    
    return ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;
}

vec3 hableToneMapping(vec3 color) {
    const float exposureBias = 2.0;
    vec3 x = hable(exposureBias * color);
    vec3 whiteScale = 1.0 / hable(vec3(11.2));
    return x * whiteScale;
}

// Apply tone mapping based on selected type
vec3 applyToneMapping(vec3 color) {
    switch(postOptions.toneMappingType) {
        case 1: return reinhardToneMapping(color);
        case 2: return acesToneMapping(color);
        case 3: return uncharted2ToneMapping(color);
        case 4: return gtToneMapping(color);
        case 5: return lottesToneMapping(color);
        case 6: return exponentialToneMapping(color);
        case 7: return reinhardExtendedToneMapping(color, postOptions.maxWhite);
        case 8: return luminanceToneMapping(color);
        case 9: return hableToneMapping(color);
        default: return color; // No tone mapping
    }
}

// ===== COLOR GRADING FUNCTIONS =====

vec3 adjustContrast(vec3 color, float contrast) {
    return (color - 0.5) * contrast + 0.5;
}

vec3 adjustSaturation(vec3 color, float saturation) {
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return mix(vec3(luminance), color, saturation);
}

vec3 adjustVibrance(vec3 color, float vibrance) {
    float maxComponent = max(max(color.r, color.g), color.b);
    float minComponent = min(min(color.r, color.g), color.b);
    float satLevel = maxComponent - minComponent;
    
    float vibranceAdjust = 1.0 + vibrance * (1.0 - satLevel);
    return mix(vec3(dot(color, vec3(0.2126, 0.7152, 0.0722))), color, vibranceAdjust);
}

// ===== EFFECT FUNCTIONS =====

vec3 applyVignette(vec3 color, vec2 uv) {
    if (postOptions.vignetteStrength <= 0.0) return color;
    
    vec2 center = vec2(0.5, 0.5);
    float distance = length(uv - center);
    float vignette = smoothstep(postOptions.vignetteRadius, postOptions.vignetteRadius - postOptions.vignetteStrength, distance);
    return color * vignette;
}

vec3 addFilmGrain(vec3 color, vec2 uv) {
    if (postOptions.filmGrainStrength <= 0.0) return color;
    
    // Simple noise function
    float noise = fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453);
    noise = (noise - 0.5) * postOptions.filmGrainStrength;
    return color + noise;
}

// ===== NVIDIA IMAGE SCALING STYLE SPATIAL UPSCALER =====
//
// This low-VRAM Vulkan path follows the public NIS design principles: a six-tap
// directional reconstruction filter followed by contrast-adaptive sharpening.
// It is integrated into the existing fragment post pass to avoid allocating a
// second full-resolution storage image on 2 GB GPUs.
float nisLuma(vec3 color) {
    float linearLuma = dot(max(color, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
    return linearLuma / (1.0 + linearLuma);
}

float nisSinc(float x) {
    const float pi = 3.14159265358979323846;
    if (abs(x) < 1.0e-4) {
        return 1.0;
    }
    float pix = pi * x;
    return sin(pix) / pix;
}

float nisSixTapWeight(float distanceFromCenter) {
    float x = abs(distanceFromCenter);
    if (x >= 3.0) {
        return 0.0;
    }
    // Six-tap Lanczos reconstruction is used as a compact phase filter. Direction
    // selection below prevents the filter from unnecessarily crossing strong edges.
    return nisSinc(distanceFromCenter) * nisSinc(distanceFromCenter / 3.0);
}

bool nisUpscalingActive() {
    if (postOptions.nisEnabled == 0) {
        return false;
    }

    vec2 inputSize = vec2(textureSize(floatColor2, 0));
    vec2 outputSize = 1.0 / max(fwidth(inTexCoord), vec2(1.0e-6));
    vec2 enlargement = outputSize / max(inputSize, vec2(1.0));
    return max(enlargement.x, enlargement.y) >= postOptions.nisScaleThreshold;
}

vec3 nisSpatialUpscale(vec2 uv) {
    vec2 inputSize = vec2(textureSize(floatColor2, 0));
    vec2 texel = 1.0 / inputSize;

    vec3 center = texture(floatColor2, uv).rgb;
    vec3 north  = texture(floatColor2, uv + vec2(0.0, -texel.y)).rgb;
    vec3 south  = texture(floatColor2, uv + vec2(0.0,  texel.y)).rgb;
    vec3 west   = texture(floatColor2, uv + vec2(-texel.x, 0.0)).rgb;
    vec3 east   = texture(floatColor2, uv + vec2( texel.x, 0.0)).rgb;

    float gradientX = abs(nisLuma(east) - nisLuma(west));
    float gradientY = abs(nisLuma(south) - nisLuma(north));

    // Reconstruct along the detected edge tangent. Vertical edges therefore use
    // vertical taps and horizontal edges use horizontal taps, reducing edge blur.
    vec2 axis = gradientX > gradientY ? vec2(0.0, 1.0) : vec2(1.0, 0.0);
    vec2 sourcePosition = uv * inputSize - vec2(0.5);
    float phase = axis.x > 0.5 ? fract(sourcePosition.x) : fract(sourcePosition.y);

    vec2 phaseOrigin = uv;
    if (axis.x > 0.5) {
        phaseOrigin.x = (floor(sourcePosition.x) + 0.5) / inputSize.x;
    } else {
        phaseOrigin.y = (floor(sourcePosition.y) + 0.5) / inputSize.y;
    }

    vec3 reconstructed = vec3(0.0);
    float totalWeight = 0.0;
    for (int tap = -2; tap <= 3; ++tap) {
        float weight = nisSixTapWeight(float(tap) - phase);
        vec2 sampleUv = clamp(phaseOrigin + axis * float(tap) * texel,
                              vec2(0.5) * texel, vec2(1.0) - vec2(0.5) * texel);
        reconstructed += texture(floatColor2, sampleUv).rgb * weight;
        totalWeight += weight;
    }
    reconstructed /= max(totalWeight, 1.0e-5);

    // NIS-style contrast-adaptive unsharp mask. The clamp limits halos and ringing
    // around Bistro roof lines, window frames and high-contrast UI-adjacent edges.
    vec3 localBlur = (north + south + west + east) * 0.25;
    float minLuma = min(nisLuma(center),
                        min(min(nisLuma(north), nisLuma(south)),
                            min(nisLuma(west), nisLuma(east))));
    float maxLuma = max(nisLuma(center),
                        max(max(nisLuma(north), nisLuma(south)),
                            max(nisLuma(west), nisLuma(east))));
    float localContrast = (maxLuma - minLuma) / max(maxLuma, 0.05);
    float edgeConfidence = smoothstep(0.015, 0.22, localContrast);
    float sharpenAmount = clamp(postOptions.nisSharpness, 0.0, 1.0) * edgeConfidence;

    vec3 sharpened = reconstructed + (reconstructed - localBlur) * sharpenAmount;
    vec3 localMin = min(center, min(min(north, south), min(west, east)));
    vec3 localMax = max(center, max(max(north, south), max(west, east)));
    vec3 localRange = max(localMax - localMin, vec3(1.0e-4));
    return clamp(sharpened, localMin - localRange * 0.06, localMax + localRange * 0.06);
}

// ===== ADVANCED FXAA IMPLEMENTATION =====

float fxaaLuma(vec3 rgb) {
    // Compress HDR luminance before edge comparison. This keeps the thresholds stable
    // before tone mapping and prevents bright Bistro highlights from hiding thin edges.
    float linearLuma = dot(max(rgb, vec3(0.0)), vec3(0.299, 0.587, 0.114));
    return linearLuma / (1.0 + linearLuma);
}

// Full-resolution edge resolve for the temporally reconstructed FSR output.
// The temporal pass removes frame-to-frame instability; this deterministic spatial
// pass removes the remaining geometric stair steps without feeding filtered pixels
// back into history. It follows the high-quality FXAA edge-endpoint search rather
// than the short directional blur used by the low-cost fallback below.
vec3 sampleTemporalOutput(vec2 uv) {
    vec2 texelSize = 1.0 / vec2(textureSize(temporalOutput, 0));
    vec2 halfTexel = texelSize * 0.5;
    return texture(temporalOutput, clamp(uv, halfTexel, vec2(1.0) - halfTexel)).rgb;
}

vec3 resolveTemporalEdgesUltra(vec2 uv) {
    vec2 texelSize = 1.0 / vec2(textureSize(temporalOutput, 0));

    vec3 rgbM  = sampleTemporalOutput(uv);
    vec3 rgbN  = sampleTemporalOutput(uv + vec2(0.0, -texelSize.y));
    vec3 rgbS  = sampleTemporalOutput(uv + vec2(0.0,  texelSize.y));
    vec3 rgbW  = sampleTemporalOutput(uv + vec2(-texelSize.x, 0.0));
    vec3 rgbE  = sampleTemporalOutput(uv + vec2( texelSize.x, 0.0));
    vec3 rgbNW = sampleTemporalOutput(uv + vec2(-texelSize.x, -texelSize.y));
    vec3 rgbNE = sampleTemporalOutput(uv + vec2( texelSize.x, -texelSize.y));
    vec3 rgbSW = sampleTemporalOutput(uv + vec2(-texelSize.x,  texelSize.y));
    vec3 rgbSE = sampleTemporalOutput(uv + vec2( texelSize.x,  texelSize.y));

    float lumaM  = fxaaLuma(rgbM);
    float lumaN  = fxaaLuma(rgbN);
    float lumaS  = fxaaLuma(rgbS);
    float lumaW  = fxaaLuma(rgbW);
    float lumaE  = fxaaLuma(rgbE);
    float lumaNW = fxaaLuma(rgbNW);
    float lumaNE = fxaaLuma(rgbNE);
    float lumaSW = fxaaLuma(rgbSW);
    float lumaSE = fxaaLuma(rgbSE);

    float lumaMin = min(lumaM, min(min(min(lumaN, lumaS), min(lumaW, lumaE)),
                                   min(min(lumaNW, lumaNE), min(lumaSW, lumaSE))));
    float lumaMax = max(lumaM, max(max(max(lumaN, lumaS), max(lumaW, lumaE)),
                                   max(max(lumaNW, lumaNE), max(lumaSW, lumaSE))));
    float lumaRange = lumaMax - lumaMin;

    // Aggressive but contrast-relative thresholds catch distant rails, roof lines and
    // window frames. Flat surfaces leave here after the initial 3x3 neighborhood.
    if (lumaRange < max(1.0 / 48.0, lumaMax * 0.045)) {
        return rgbM;
    }

    float edgeHorizontal =
        abs(-2.0 * lumaN + lumaNW + lumaNE) +
        2.0 * abs(-2.0 * lumaM + lumaW + lumaE) +
        abs(-2.0 * lumaS + lumaSW + lumaSE);
    float edgeVertical =
        abs(-2.0 * lumaW + lumaNW + lumaSW) +
        2.0 * abs(-2.0 * lumaM + lumaN + lumaS) +
        abs(-2.0 * lumaE + lumaNE + lumaSE);
    bool horizontal = edgeHorizontal >= edgeVertical;

    // Keep a continuous edge normal instead of reducing every silhouette to either
    // horizontal or vertical. The old binary choice left visible pixel stairs on
    // oblique roof lines and at the corners of windows and building silhouettes.
    vec2 sobelGradient = vec2(
        (lumaNE + 2.0 * lumaE + lumaSE) -
        (lumaNW + 2.0 * lumaW + lumaSW),
        (lumaSW + 2.0 * lumaS + lumaSE) -
        (lumaNW + 2.0 * lumaN + lumaNE));
    float sobelLength = length(sobelGradient);
    vec2 continuousNormal = sobelLength > 1.0e-6
        ? sobelGradient / sobelLength
        : (horizontal ? vec2(0.0, 1.0) : vec2(1.0, 0.0));

    // A structure tensor distinguishes a real corner (two strong gradient axes)
    // from a straight diagonal edge (one coherent axis). Only true corners receive
    // the wider area-coverage resolve below, so flat texture detail stays sharp.
    vec2 gradientNW = 0.5 * vec2(
        (lumaN + lumaM) - (lumaNW + lumaW),
        (lumaW + lumaM) - (lumaNW + lumaN));
    vec2 gradientNE = 0.5 * vec2(
        (lumaNE + lumaE) - (lumaN + lumaM),
        (lumaM + lumaE) - (lumaN + lumaNE));
    vec2 gradientSW = 0.5 * vec2(
        (lumaM + lumaS) - (lumaW + lumaSW),
        (lumaSW + lumaS) - (lumaW + lumaM));
    vec2 gradientSE = 0.5 * vec2(
        (lumaE + lumaSE) - (lumaM + lumaS),
        (lumaS + lumaSE) - (lumaM + lumaE));

    float tensorXX = dot(vec4(gradientNW.x, gradientNE.x,
                              gradientSW.x, gradientSE.x),
                         vec4(gradientNW.x, gradientNE.x,
                              gradientSW.x, gradientSE.x));
    float tensorYY = dot(vec4(gradientNW.y, gradientNE.y,
                              gradientSW.y, gradientSE.y),
                         vec4(gradientNW.y, gradientNE.y,
                              gradientSW.y, gradientSE.y));
    float tensorXY = dot(vec4(gradientNW.x, gradientNE.x,
                              gradientSW.x, gradientSE.x),
                         vec4(gradientNW.y, gradientNE.y,
                              gradientSW.y, gradientSE.y));
    float tensorTrace = tensorXX + tensorYY;
    float tensorDiscriminant = sqrt(max(
        tensorTrace * tensorTrace -
        4.0 * max(tensorXX * tensorYY - tensorXY * tensorXY, 0.0), 0.0));
    float lambdaMin = 0.5 * (tensorTrace - tensorDiscriminant);
    float lambdaMax = 0.5 * (tensorTrace + tensorDiscriminant);
    float cornerRatio = lambdaMin / max(lambdaMax, 1.0e-6);
    float cornerConfidence = smoothstep(0.035, 0.20, cornerRatio);

    float lumaSide1 = horizontal ? lumaN : lumaW;
    float lumaSide2 = horizontal ? lumaS : lumaE;
    float gradient1 = abs(lumaSide1 - lumaM);
    float gradient2 = abs(lumaSide2 - lumaM);
    bool side1Steeper = gradient1 >= gradient2;
    float scaledGradient = max(gradient1, gradient2) * 0.25;

    float normalStep = horizontal ? texelSize.y : texelSize.x;
    float lumaLocalAverage;
    if (side1Steeper) {
        normalStep = -normalStep;
        lumaLocalAverage = 0.5 * (lumaSide1 + lumaM);
    } else {
        lumaLocalAverage = 0.5 * (lumaSide2 + lumaM);
    }

    vec2 edgeUv = uv;
    if (horizontal) {
        edgeUv.y += normalStep * 0.5;
    } else {
        edgeUv.x += normalStep * 0.5;
    }

    vec2 tangentStep = horizontal ? vec2(texelSize.x, 0.0)
                                  : vec2(0.0, texelSize.y);
    vec2 endpointUv1 = edgeUv - tangentStep;
    vec2 endpointUv2 = edgeUv + tangentStep;
    float endpointLuma1 = fxaaLuma(sampleTemporalOutput(endpointUv1)) - lumaLocalAverage;
    float endpointLuma2 = fxaaLuma(sampleTemporalOutput(endpointUv2)) - lumaLocalAverage;
    bool endpointFound1 = abs(endpointLuma1) >= scaledGradient;
    bool endpointFound2 = abs(endpointLuma2) >= scaledGradient;

    // Quality preset: search up to 12 output pixels in both directions. The loop exits
    // independently on each side, keeping the common short-edge case inexpensive.
    for (int stepIndex = 0; stepIndex < 12; ++stepIndex) {
        if (!endpointFound1) {
            endpointUv1 -= tangentStep;
            endpointLuma1 =
                fxaaLuma(sampleTemporalOutput(endpointUv1)) - lumaLocalAverage;
            endpointFound1 = abs(endpointLuma1) >= scaledGradient;
        }
        if (!endpointFound2) {
            endpointUv2 += tangentStep;
            endpointLuma2 =
                fxaaLuma(sampleTemporalOutput(endpointUv2)) - lumaLocalAverage;
            endpointFound2 = abs(endpointLuma2) >= scaledGradient;
        }
        if (endpointFound1 && endpointFound2) {
            break;
        }
    }

    float distance1 = horizontal ? abs(uv.x - endpointUv1.x)
                                 : abs(uv.y - endpointUv1.y);
    float distance2 = horizontal ? abs(endpointUv2.x - uv.x)
                                 : abs(endpointUv2.y - uv.y);
    bool endpoint1Closer = distance1 < distance2;
    float closestDistance = min(distance1, distance2);
    float edgeLength = max(distance1 + distance2, 1.0e-6);
    float edgeOffset = 0.5 - closestDistance / edgeLength;

    float closestEndpointLuma = endpoint1Closer ? endpointLuma1 : endpointLuma2;
    bool centerIsDarker = lumaM < lumaLocalAverage;
    bool endpointVariationCorrect = (closestEndpointLuma < 0.0) != centerIsDarker;
    if (!endpointVariationCorrect) {
        edgeOffset = 0.0;
    }

    // Sub-pixel coverage handles isolated one-pixel steps that have no long endpoint.
    float neighborhoodLuma =
        (2.0 * (lumaN + lumaS + lumaW + lumaE) +
         lumaNW + lumaNE + lumaSW + lumaSE) / 12.0;
    float subpixelOffset =
        clamp(abs(neighborhoodLuma - lumaM) / max(lumaRange, 1.0e-5), 0.0, 1.0);
    subpixelOffset = subpixelOffset * subpixelOffset *
                     (3.0 - 2.0 * subpixelOffset);
    subpixelOffset = subpixelOffset * subpixelOffset * 0.88;

    float finalOffset = max(edgeOffset, subpixelOffset);
    vec2 axisNormal = horizontal
        ? vec2(0.0, normalStep < 0.0 ? -1.0 : 1.0)
        : vec2(normalStep < 0.0 ? -1.0 : 1.0, 0.0);
    if (dot(continuousNormal, axisNormal) < 0.0) {
        continuousNormal = -continuousNormal;
    }

    // Blend toward the real sub-pixel normal as the edge becomes diagonal. This
    // prevents the one-pixel horizontal/vertical jumps that formed a saw-tooth line.
    vec2 absoluteNormal = abs(continuousNormal);
    float diagonalAmount = min(absoluteNormal.x, absoluteNormal.y) /
                           max(max(absoluteNormal.x, absoluteNormal.y), 1.0e-6);
    float continuousWeight = smoothstep(0.08, 0.42, diagonalAmount) * 0.90;
    vec2 resolvedNormal = normalize(mix(axisNormal, continuousNormal,
                                        continuousWeight));
    vec2 finalUv = uv + resolvedNormal * texelSize * finalOffset;
    vec3 lineResolved = sampleTemporalOutput(finalUv);

    // Four rotated-grid coverage samples approximate the fractional pixel area at
    // diagonal steps and corners. This runs only after a confirmed edge and blends
    // most strongly at true corners, avoiding a full-screen softening pass.
    vec3 areaResolved =
        sampleTemporalOutput(finalUv + texelSize * vec2(-0.375, -0.125)) +
        sampleTemporalOutput(finalUv + texelSize * vec2( 0.125, -0.375)) +
        sampleTemporalOutput(finalUv + texelSize * vec2( 0.375,  0.125)) +
        sampleTemporalOutput(finalUv + texelSize * vec2(-0.125,  0.375));
    areaResolved *= 0.25;

    vec3 neighborhoodMin = min(rgbM, min(min(min(rgbN, rgbS), min(rgbW, rgbE)),
                                         min(min(rgbNW, rgbNE), min(rgbSW, rgbSE))));
    vec3 neighborhoodMax = max(rgbM, max(max(max(rgbN, rgbS), max(rgbW, rgbE)),
                                         max(max(rgbNW, rgbNE), max(rgbSW, rgbSE))));
    areaResolved = clamp(areaResolved, neighborhoodMin, neighborhoodMax);

    float areaCoverage = clamp(diagonalAmount * 0.45 +
                               cornerConfidence * 0.55, 0.0, 0.86);
    return mix(lineResolved, areaResolved, areaCoverage);
}

// Directional 9-tap FXAA. Strength adjusts edge sensitivity and final coverage,
// while the tap count stays fixed so frame cost is predictable on low-VRAM GPUs.
vec3 fxaaAdvanced(vec2 uv, float fxaaStrength) {
    vec2 texelSize = 1.0 / vec2(textureSize(floatColor2, 0));
    float strength = clamp(fxaaStrength, 0.0, 1.0);

    vec3 rgbM  = texture(floatColor2, uv).rgb;
    vec3 rgbN  = texture(floatColor2, uv + vec2(0.0, -texelSize.y)).rgb;
    vec3 rgbS  = texture(floatColor2, uv + vec2(0.0,  texelSize.y)).rgb;
    vec3 rgbW  = texture(floatColor2, uv + vec2(-texelSize.x, 0.0)).rgb;
    vec3 rgbE  = texture(floatColor2, uv + vec2( texelSize.x, 0.0)).rgb;
    vec3 rgbNW = texture(floatColor2, uv + vec2(-texelSize.x, -texelSize.y)).rgb;
    vec3 rgbNE = texture(floatColor2, uv + vec2( texelSize.x, -texelSize.y)).rgb;
    vec3 rgbSW = texture(floatColor2, uv + vec2(-texelSize.x,  texelSize.y)).rgb;
    vec3 rgbSE = texture(floatColor2, uv + vec2( texelSize.x,  texelSize.y)).rgb;

    float lumaM  = fxaaLuma(rgbM);
    float lumaN  = fxaaLuma(rgbN);
    float lumaS  = fxaaLuma(rgbS);
    float lumaW  = fxaaLuma(rgbW);
    float lumaE  = fxaaLuma(rgbE);
    float lumaNW = fxaaLuma(rgbNW);
    float lumaNE = fxaaLuma(rgbNE);
    float lumaSW = fxaaLuma(rgbSW);
    float lumaSE = fxaaLuma(rgbSE);

    float lumaMin = min(lumaM, min(min(min(lumaN, lumaS), min(lumaW, lumaE)),
                                   min(min(lumaNW, lumaNE), min(lumaSW, lumaSE))));
    float lumaMax = max(lumaM, max(max(max(lumaN, lumaS), max(lumaW, lumaE)),
                                   max(max(lumaNW, lumaNE), max(lumaSW, lumaSE))));
    float lumaRange = lumaMax - lumaMin;

    // Lower thresholds than the previous implementation catch railings, roof lines,
    // foliage and distant geometry without applying a full-screen blur.
    float relativeThreshold = mix(0.110, 0.040, strength);
    float absoluteThreshold = mix(0.045, 0.012, strength);
    if (lumaRange < max(absoluteThreshold, lumaMax * relativeThreshold)) {
        return rgbM;
    }

    vec2 direction;
    direction.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    direction.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float neighborhoodLuma =
        (lumaN + lumaS + lumaW + lumaE + lumaNW + lumaNE + lumaSW + lumaSE) * 0.125;
    float directionReduce =
        max(neighborhoodLuma * mix(0.060, 0.025, strength), 1.0 / 256.0);
    float inverseDirection =
        1.0 / (min(abs(direction.x), abs(direction.y)) + directionReduce);
    float span = mix(6.0, 10.0, strength);
    direction = clamp(direction * inverseDirection, vec2(-span), vec2(span)) * texelSize;

    vec3 rgbA = 0.5 * (
        texture(floatColor2, uv + direction * (1.0 / 3.0 - 0.5)).rgb +
        texture(floatColor2, uv + direction * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        texture(floatColor2, uv + direction * -0.5).rgb +
        texture(floatColor2, uv + direction *  0.5).rgb);

    float lumaB = fxaaLuma(rgbB);
    vec3 directionalResult = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;

    // Sub-pixel coverage removes small stair steps and shimmering that the directional
    // search alone can miss. Preserve detail by blending only on confirmed edges.
    float subpixel = clamp(abs(neighborhoodLuma - lumaM) / max(lumaRange, 1.0e-5), 0.0, 1.0);
    subpixel = subpixel * subpixel * (3.0 - 2.0 * subpixel);
    float coverage = max(mix(0.62, 0.92, strength),
                         subpixel * mix(0.70, 0.95, strength));
    return mix(rgbM, directionalResult, coverage);
}

vec3 applyChromaticAberrationOrFXAA(vec2 uv) {
    // NVScaler already combines spatial reconstruction and sharpening. Avoid stacking
    // the multi-tap FXAA path while upscaling, which would blur the reconstructed edge.
    if (nisUpscalingActive()) {
        return nisSpatialUpscale(uv);
    }

    if (postOptions.chromaticAberration > 1.0) {
        // FXAA mode: values 1.0-2.0 map directly to smoothing strength 0.0-1.0.
        float fxaaStrength = postOptions.chromaticAberration - 1.0;
        return fxaaAdvanced(uv, clamp(fxaaStrength, 0.0, 1.0));
    } else if (postOptions.chromaticAberration > 0.0) {
        // Chromatic aberration mode: 0.0-1.0 range
        vec2 center = vec2(0.5, 0.5);
        vec2 offset = (uv - center) * postOptions.chromaticAberration * 0.01;
        
        float r = texture(floatColor2, uv + offset).r;
        float g = texture(floatColor2, uv).g;
        float b = texture(floatColor2, uv - offset).b;
        
        return vec3(r, g, b);
    } else {
        // No effect
        return texture(floatColor2, uv).rgb;
    }
}

// ===== DEBUG FUNCTIONS =====

vec3 showColorChannel(vec3 color) {
    switch(postOptions.showOnlyChannel) {
        case 1: return vec3(color.r, 0.0, 0.0); // Red only
        case 2: return vec3(0.0, color.g, 0.0); // Green only
        case 3: return vec3(0.0, 0.0, color.b); // Blue only
        case 4: return vec3(1.0); // Alpha channel not available for vec3, show white
        case 5: return vec3(dot(color, vec3(0.2126, 0.7152, 0.0722))); // Luminance
        default: return color; // All channels
    }
}

vec3 toneMappingComparison(vec3 originalColor, vec2 uv) {
    // Show different tone mapping methods side by side
    float sections = 9.0;
    float sectionWidth = 1.0 / sections;
    int section = int(uv.x / sectionWidth);
    
    vec3 toneMapped;
    switch(section) {
        case 0: toneMapped = originalColor; break; // No tone mapping
        case 1: toneMapped = reinhardToneMapping(originalColor); break;
        case 2: toneMapped = acesToneMapping(originalColor); break;
        case 3: toneMapped = uncharted2ToneMapping(originalColor); break;
        case 4: toneMapped = gtToneMapping(originalColor); break;
        case 5: toneMapped = lottesToneMapping(originalColor); break;
        case 6: toneMapped = exponentialToneMapping(originalColor); break;
        case 7: toneMapped = reinhardExtendedToneMapping(originalColor, postOptions.maxWhite); break;
        case 8: toneMapped = hableToneMapping(originalColor); break;
        default: toneMapped = originalColor; break;
    }
    
    // Add labels (simple color coding)
    if (uv.y < 0.05) {
        vec3 labelColors[9] = vec3[](
            vec3(1.0, 1.0, 1.0), // None - White
            vec3(1.0, 0.0, 0.0), // Reinhard - Red
            vec3(0.0, 1.0, 0.0), // ACES - Green
            vec3(0.0, 0.0, 1.0), // Uncharted2 - Blue
            vec3(1.0, 1.0, 0.0), // GT - Yellow
            vec3(1.0, 0.0, 1.0), // Lottes - Magenta
            vec3(0.0, 1.0, 1.0), // Exponential - Cyan
            vec3(1.0, 0.5, 0.0), // ReinhardExt - Orange
            vec3(0.5, 0.0, 1.0)  // Hable - Purple
        );
        return labelColors[section];
    }
    
    return toneMapped;
}

// ===== BOKEH DEPTH OF FIELD IMPLEMENTATION =====

// Decode Bokeh parameters from padding1
vec3 decodeBokehParams(float encoded) {
    // Encode: focusDistance (0-100) + aperture (0-10) + intensity (0-10)
    // Format: FFFFAAAAII where F=focus(2digits), A=aperture(2digits), I=intensity(2digits)
    float focusDistance = floor(encoded / 10000.0);
    float aperture = floor(mod(encoded, 10000.0) / 100.0);
    float intensity = mod(encoded, 100.0);
    
    return vec3(focusDistance / 100.0, aperture / 100.0, intensity / 100.0);
}

// Convert depth buffer value to linear depth
float linearizeDepth(float depth) {
    // Assuming reversed Z (far=0, near=1) which is common in modern engines
    float near = 0.1;  // Near plane
    float far = 256.0; // Far plane
    
    // Convert from [0,1] to linear depth
    return near / (depth * (near - far) + far);
}

// Calculate Circle of Confusion based on depth and camera parameters
float calculateCoC(float depth, vec3 bokehParams) {
    float focusDistance = bokehParams.x * 50.0 + 0.1; // 0.1 to 50.1 units
    float aperture = bokehParams.y * 0.1 + 0.001;     // 0.001 to 0.101 (f-stop simulation)
    
    float linearDepth = linearizeDepth(depth);
    
    // Thin lens equation for Circle of Confusion
    float focalLength = 0.05; // 50mm equivalent
    float cocRadius = abs(aperture * focalLength * (linearDepth - focusDistance)) / 
                     (linearDepth * (focusDistance - focalLength));
    
    // Scale CoC for screen space (adjust based on resolution)
    return clamp(cocRadius * 100.0, 0.0, 20.0); // Max 20 pixel radius
}

// Hexagonal sampling pattern for high-quality Bokeh
vec2 hexSample(int index, float radius) {
    const vec2 hexOffsets[19] = vec2[](
        vec2(0.0, 0.0),
        // Ring 1 (6 samples)
        vec2(1.0, 0.0), vec2(0.5, 0.866), vec2(-0.5, 0.866),
        vec2(-1.0, 0.0), vec2(-0.5, -0.866), vec2(0.5, -0.866),
        // Ring 2 (12 samples)
        vec2(2.0, 0.0), vec2(1.5, 0.866), vec2(1.0, 1.732), vec2(0.0, 2.0),
        vec2(-1.0, 1.732), vec2(-1.5, 0.866), vec2(-2.0, 0.0), vec2(-1.5, -0.866),
        vec2(-1.0, -1.732), vec2(0.0, -2.0), vec2(1.0, -1.732), vec2(1.5, -0.866)
    );
    
    return hexOffsets[index] * radius;
}

// Enhanced Bokeh blur with quality-based sampling
vec3 applyBokeh(vec2 uv, vec3 bokehParams) {
    float intensity = bokehParams.z;
    
    if (intensity <= 0.0) {
        return texture(floatColor2, uv).rgb;
    }
    
    vec2 texelSize = 1.0 / textureSize(floatColor2, 0);
    float centerDepth = texture(depthStencil, uv).r;
    float centerCoC = calculateCoC(centerDepth, bokehParams);
    
    // Early exit for sharp pixels
    if (centerCoC < 0.5) {
        return texture(floatColor2, uv).rgb;
    }
    
    vec3 centerColor = texture(floatColor2, uv).rgb;
    vec3 blurredColor = vec3(0.0);
    float totalWeight = 0.0;
    
    // Adaptive sample count based on CoC size
    int sampleCount = int(clamp(centerCoC * 2.0 + 7.0, 7.0, 19.0));
    
    for (int i = 0; i < sampleCount; i++) {
        vec2 offset = hexSample(i, centerCoC * texelSize.x);
        vec2 sampleUV = uv + offset;
        
        // Bounds check
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) {
            continue;
        }
        
        float sampleDepth = texture(depthStencil, sampleUV).r;
        float sampleCoC = calculateCoC(sampleDepth, bokehParams);
        vec3 sampleColor = texture(floatColor2, sampleUV).rgb;
        
        // Weight based on depth relationship and CoC overlap
        float depthWeight = 1.0;
        if (sampleDepth > centerDepth) {
            // Background pixel: full contribution if its CoC covers center
            depthWeight = smoothstep(0.0, centerCoC, sampleCoC);
        } else {
            // Foreground pixel: contributes based on its own CoC
            depthWeight = smoothstep(0.0, centerCoC, sampleCoC);
        }
        
        // Enhance bright areas for Bokeh highlights
        float brightness = dot(sampleColor, vec3(0.299, 0.587, 0.114));
        float bokehHighlight = 1.0 + smoothstep(0.7, 1.2, brightness) * intensity;
        
        float weight = depthWeight * bokehHighlight;
        blurredColor += sampleColor * weight;
        totalWeight += weight;
    }
    
    if (totalWeight > 0.0) {
        blurredColor /= totalWeight;
        // Blend between original and blurred based on CoC and intensity
        float blendFactor = smoothstep(0.0, 2.0, centerCoC) * intensity;
        return mix(centerColor, blurredColor, blendFactor);
    }
    
    return centerColor;
}

// Debug visualization for Bokeh effect
vec3 visualizeBokehDebug(vec2 uv, vec3 bokehParams) {
    float depth = texture(depthStencil, uv).r;
    float coc = calculateCoC(depth, bokehParams);
    
    // Visualize CoC as heat map
    vec3 heatMap = vec3(0.0);
    if (coc < 1.0) {
        heatMap = vec3(0.0, 1.0, 0.0); // Green for sharp areas
    } else if (coc < 5.0) {
        float t = (coc - 1.0) / 4.0;
        heatMap = mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), t); // Green to Yellow
    } else {
        float t = clamp((coc - 5.0) / 15.0, 0.0, 1.0);
        heatMap = mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), t); // Yellow to Red
    }
    
    return heatMap;
}

void main() {
    vec2 uv = inTexCoord;
    
    // FSR already provides temporal stability. Resolve the remaining silhouette and
    // sub-pixel stair steps at presentation resolution, after temporal reconstruction.
    vec3 originalColor = postOptions.nisEnabled == 0
        ? resolveTemporalEdgesUltra(uv)
        : applyChromaticAberrationOrFXAA(uv);
    
    // Apply Bokeh depth of field if enabled
    vec3 bokehParams = decodeBokehParams(postOptions.padding1);
    if (bokehParams.z > 0.0 && postOptions.nisEnabled != 0) {
        originalColor = applyBokeh(uv, bokehParams);
    }
    
    // Apply exposure
    vec3 color = originalColor * postOptions.exposure;
    
    // Debug mode: tone mapping comparison
    if (postOptions.debugMode == 1) {
        color = toneMappingComparison(color, uv);
    } else if (postOptions.debugMode == 4) {
        // New debug mode for Bokeh visualization
        color = visualizeBokehDebug(uv, bokehParams);
    } else {
        // Apply tone mapping
        color = applyToneMapping(color);
    }
    
    // Color grading
    color = adjustContrast(color, postOptions.contrast);
    color = color + postOptions.brightness; // Brightness adjustment
    color = adjustSaturation(color, postOptions.saturation);
    color = adjustVibrance(color, postOptions.vibrance);
    
    // Effects
    color = applyVignette(color, uv);
    color = addFilmGrain(color, uv);
    
    // Debug: show individual color channels
    if (postOptions.debugMode == 2) {
        color = showColorChannel(color);
    }
    
    // Gamma correction
    color = pow(max(color, vec3(0.0)), vec3(1.0 / postOptions.gamma));
    
    // Debug split comparison
    if (postOptions.debugMode == 3 && uv.x > postOptions.debugSplit) {
        // Right side: processed
        outFragColor = vec4(color, 1.0);
    } else if (postOptions.debugMode == 3) {
        // Left side: original (with basic exposure and gamma)
        vec3 simple = pow(originalColor * postOptions.exposure, vec3(1.0 / postOptions.gamma));
        outFragColor = vec4(simple, 1.0);
    } else {
        outFragColor = vec4(color, 1.0);
    }
    
    // Add a thin line at the split position for debug mode 3
    if (postOptions.debugMode == 3 && abs(uv.x - postOptions.debugSplit) < 0.002) {
        outFragColor = vec4(1.0, 1.0, 0.0, 1.0); // Yellow line
    }
}
