"""Derive both full Custom-node shaders from the preserved FogSafe sources."""
from pathlib import Path
import json

project = Path(__file__).resolve().parents[2]
texture = json.loads((project / 'SourceArt/BlackHole/T_BH_DiskStructure.json').read_text())
filament_mean = texture['channels']['Filaments']['mean']
flow_bound = texture['flow_jacobian_bound']

setup = '''
// Coherent flow structures. The zero branch retains the original disk calculation.
float MorphAmount = saturate(StructureAmount);
float MorphScale = clamp(floor(StructureScale + 0.5), 1.0, 8.0);
float Filaments = clamp(FilamentStrength, 0.0, 3.0);
float Clumps = clamp(ClumpStrength, 0.0, 4.0);
float Lifetime = clamp(StructureLifetime, 1.0, 30.0);
float Vortices = clamp(VortexStrength, 0.0, 0.5);
float DensityChange = clamp(DensityVariation, 0.0, 0.75);
float ThicknessChange = clamp(ThicknessVariation, 0.0, 0.5);
float PhaseTime = Time / Lifetime;
float2 FlowCycles = frac(float2(PhaseTime, PhaseTime + 0.5));
float2 FlowIds = floor(float2(PhaseTime, PhaseTime + 0.5));
float FlowWeight = sin(3.14159265 * FlowCycles.x);
FlowWeight *= FlowWeight;
float2 FlowWeights = float2(FlowWeight, 1.0 - FlowWeight);
float2 FlowOffsets[2];
FlowOffsets[0] = frac(sin(float2(FlowIds.x * 127.1 + 17.7, FlowIds.x * 311.7 + 61.2)) * 43758.5453);
FlowOffsets[1] = frac(sin(float2(FlowIds.y * 127.1 + 91.3, FlowIds.y * 311.7 + 23.8)) * 43758.5453);
float2 StructureSize = float2(1, 1);
float StructureLastMip = 0.0;
if (MorphAmount > 0.0)
{
    uint StructureWidth, StructureHeight;
    DiskStructure.GetDimensions(StructureWidth, StructureHeight);
    StructureSize = max(float2(StructureWidth, StructureHeight), float2(1, 1));
    StructureLastMip = log2(max(StructureSize.x, StructureSize.y));
}

// Gaussian integral along a segment. A&S 7.1.26 avoids extra ray samples.
struct DiskProfileMath
{
    float Erf(float X)
    {
        float A = min(abs(X), 5.0);
        float T = rcp(1.0 + 0.3275911 * A);
        float P = (((((1.061405429 * T - 1.453152027) * T)
            + 1.421413741) * T - 0.284496736) * T + 0.254829592) * T;
        float E = 1.0 - P * exp(-A * A);
        return X < 0.0 ? -E : E;
    }
    float Segment(float H0, float H1, float Sigma, float SegmentLength)
    {
        float A = H0 / Sigma;
        float B = H1 / Sigma;
        if (abs(B - A) < 0.01)
            return SegmentLength * exp(-0.25 * (A + B) * (A + B));
        return SegmentLength * (0.886226925 * Sigma)
            * abs(Erf(B) - Erf(A)) / max(abs(H1 - H0), 1e-7);
    }
};
DiskProfileMath ProfileMath;
'''

candidate = '''
    float SegmentH0 = dot(PreviousPosition, Axis);
    float SegmentH1 = dot(NextPosition, Axis);
    float MaxVolumeHeight = 3.0 * min(Thickness * (1.0 + MorphAmount * ThicknessChange), 0.20)
        + 0.20 * Thickness * MorphAmount * ThicknessChange;
    bool DiskSegment = MorphAmount > 0.0
        ? min(min(SegmentH0, SegmentH1), Height) <= MaxVolumeHeight
            && max(max(SegmentH0, SegmentH1), Height) >= -MaxVolumeHeight
        : abs(Height) <= Thickness * 3.0;
    if (RadialMask > 0.0 && DiskSegment)
'''

field = '''
        float DensityMultiplier = 1.0;
        if (MorphAmount > 0.0)
        {
            float LocalOmega = RotationSpeed / max(pow(Radius, 1.5), 0.15);
            float BaseOmega = RotationSpeed / max(pow(ReferenceRadius, 1.5), 0.15);
            float3 RadialUnit = PlanarPosition / max(Radius, 1e-5);
            float3 AngularUnit = cross(Axis, RadialUnit);
            float RadialFoot = max(max(abs(dot(ScreenDx, RadialUnit)), abs(dot(ScreenDy, RadialUnit))),
                0.5 * abs(dot(SegmentVector, RadialUnit)));
            float AngularFoot = max(max(abs(dot(ScreenDx, AngularUnit)), abs(dot(ScreenDy, AngularUnit))),
                0.5 * abs(dot(SegmentVector, AngularUnit))) / max(Radius, 1e-5);
            float OmegaSlope = pow(Radius, 1.5) > 0.15
                ? 1.5 * abs(RotationSpeed) / max(pow(Radius, 2.5), 1e-5) : 0.0;
            float2 Pattern = float2(0, 0);
            [unroll]
            for (int Layer = 0; Layer < 2; ++Layer)
            {
                float Age = (FlowCycles[Layer] - 0.5) * Lifetime;
                float FlowAngle = Angle - Time * BaseOmega - (LocalOmega - BaseOmega) * Age;
                float2 UV = float2(FlowAngle * (MorphScale / 6.28318531), RadialFraction) + FlowOffsets[Layer];
                float2 Foot = float2((AngularFoot + OmegaSlope * abs(Age) * RadialFoot)
                    * (MorphScale / 6.28318531), RadialFoot / (Outer - Inner));
                float2 TexelFoot = Foot * StructureSize;
                float FlowMip = clamp(log2(max(max(TexelFoot.x, TexelFoot.y), 1.0)), 0.0, StructureLastMip);
                float2 Flow = Texture2DSampleLevel(DiskStructure, DiskStructureSampler, UV, FlowMip).rg * 2.0 - 1.0;
                float Drift = Vortices * (FlowCycles[Layer] - 0.5);
                float Stretch = 1.0 + FLOW_BOUND * abs(Drift);
                float MaskMip = clamp(FlowMip + log2(Stretch), 0.0, StructureLastMip);
                float2 Mask = Texture2DSampleLevel(DiskStructure, DiskStructureSampler, UV - Drift * Flow, MaskMip).ba;
                Pattern += FlowWeights[Layer] * Mask;
            }
            float ClumpField = clamp(2.0 * (Pattern.y - 0.5), -1.0, 1.0);
            float FilamentField = Pattern.x - FILAMENT_MEAN;
            float EmissionGain = clamp(1.0 + 1.4 * Filaments * FilamentField
                + 1.6 * Clumps * (Pattern.y - 0.5), 0.2, 4.0);
            SampleEmission *= lerp(1.0, EmissionGain, MorphAmount);
            DensityMultiplier = max(0.25, 1.0 + MorphAmount * DensityChange * ClumpField);

            float LocalThickness = clamp(Thickness * (1.0 + MorphAmount * ThicknessChange * ClumpField), 0.005, 0.20);
            float Midplane = 0.20 * Thickness * MorphAmount * ThicknessChange * ClumpField;
            float Integrated = ProfileMath.Segment(SegmentH0 - Midplane, Height - Midplane,
                LocalThickness, length(SamplePosition - PreviousPosition));
            Integrated += ProfileMath.Segment(Height - Midplane, SegmentH1 - Midplane,
                LocalThickness, length(NextPosition - SamplePosition));
            // Retain the old midpoint profile continuously as StructureAmount approaches zero.
            VerticalMask = lerp(VerticalMask, saturate(Integrated / max(StepLength, 1e-7)), MorphAmount);
        }
'''.replace('FLOW_BOUND', f'{flow_bound:.1f}').replace('FILAMENT_MEAN', f'{filament_mean:.9f}')


def replace(code, old, new):
    assert code.count(old) == 1, old[:90]
    return code.replace(old, new)


for variant in ('NoDoppler', 'Doppler'):
    code = (project / ('Shaders/BlackHoleFogSafe_' + variant + '.hlsl')).read_text()
    code = replace(code, 'float ReferenceRadius =', setup + '\nfloat ReferenceRadius =')
    code = replace(code, '    float3 SamplePosition;', '    float3 SamplePosition;\n    float3 PreviousPosition;\n    float3 NextPosition;')
    code = replace(code, '        StepLength = StraightStep;', '''        PreviousPosition = RayOrigin + RayDirection * (Start + Index * StraightStep);
        NextPosition = PreviousPosition + RayDirection * StraightStep;
        StepLength = StraightStep;''')
    code = replace(code, '        float3 PreviousPosition =', '        PreviousPosition =')
    code = replace(code, '        float3 NextPosition =', '        NextPosition =')
    code = replace(code, '    if (RadialMask > 0.0 && abs(Height) <= Thickness * 3.0)', candidate)
    code = replace(code, '        float SampleOpacity = 1.0 - exp(', field + '\n        float SampleOpacity = 1.0 - exp(')
    code = replace(code, '-Density * RadialMask * VerticalMask', '-Density * DensityMultiplier * RadialMask * VerticalMask')
    code = '// Structured FogSafe: DiskStructure RG flow, B filaments, A clumps.\n' + code
    (project / ('Shaders/BlackHoleFogSafe_Structured_' + variant + '.hlsl')).write_text(code)
    print(variant, len(code))
