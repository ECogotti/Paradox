"""Generate one Custom shader with compile-time constants supplied by static switches."""
from pathlib import Path

project = Path(__file__).resolve().parents[2]
code = (project / 'Shaders/BlackHoleFogSafe_Structured_Doppler.hlsl').read_text()

def replace(old, new):
    global code
    assert code.count(old) == 1, (code.count(old), old[:70])
    code = code.replace(old, new)

replace('float Lens = saturate(LensingStrength);',
        'float Lens = EnableLensing > 0.5 ? saturate(LensingStrength) : 0.0;')
replace('float HaloAmount = max(HaloStrength, 0.0);',
        'float HaloAmount = EnableGlow > 0.5 ? max(HaloStrength, 0.0) : 0.0;')
replace('float DetailAmount = saturate(NoiseAmount);',
        'float DetailAmount = EnableNoise > 0.5 && EnableDisk > 0.5 ? saturate(NoiseAmount) : 0.0;')
replace('bool DopplerActive = BeamingAmount > 0.0',
        'bool DopplerActive = EnableDoppler > 0.5 && EnableDisk > 0.5 && BeamingAmount > 0.0')
replace('float MorphAmount = saturate(StructureAmount);',
        'float MorphAmount = EnableDisk > 0.5 && (EnableStructures > 0.5 || EnableVolume > 0.5)\n'
        '    ? saturate(StructureAmount) : 0.0;')
replace('float DensityChange = clamp(DensityVariation, 0.0, 0.75);',
        'float DensityChange = EnableVolume > 0.5 ? clamp(DensityVariation, 0.0, 0.75) : 0.0;')
replace('float ThicknessChange = clamp(ThicknessVariation, 0.0, 0.5);',
        'float ThicknessChange = EnableVolume > 0.5 ? clamp(ThicknessVariation, 0.0, 0.5) : 0.0;')
replace('uint NoiseWidth, NoiseHeight;\n\nDiskNoise.GetDimensions(NoiseWidth, NoiseHeight);\n\nfloat2 NoiseSize = float2(NoiseWidth, NoiseHeight);\n\nfloat LastMip = log2(max(max(NoiseSize.x, NoiseSize.y), 1.0));',
        '''float2 NoiseSize = float2(1, 1);
float LastMip = 0.0;
if (DetailAmount > 0.0)
{
    uint NoiseWidth, NoiseHeight;
    DiskNoise.GetDimensions(NoiseWidth, NoiseHeight);
    NoiseSize = float2(NoiseWidth, NoiseHeight);
    LastMip = log2(max(max(NoiseSize.x, NoiseSize.y), 1.0));
}''')
replace('int Count = Straight ? 64 : Budget;',
        'int Count = Straight ? (EnableDisk > 0.5 ? 64 : 0) : Budget;')
replace('    if (RadialMask > 0.0 && DiskSegment)',
        '    if (EnableDisk > 0.5 && RadialMask > 0.0 && DiskSegment)')
replace('            SampleEmission *= lerp(1.0, EmissionGain, MorphAmount);',
        '            if (EnableStructures > 0.5)\n'
        '                SampleEmission *= lerp(1.0, EmissionGain, MorphAmount);')
replace('float Refraction = saturate(RefractionStrength);',
        'float Refraction = EnableRefraction > 0.5 && EnableLensing > 0.5 ? saturate(RefractionStrength) : 0.0;')
code = ('// Modular FogSafe: Enable* inputs MUST be static-switch constants (0/1).\n'
        '// Runtime selects a cooked MIC, never changes a static switch on the MID.\n' + code)
(project / 'Shaders/BlackHoleFogSafe_Modular.hlsl').write_text(code)
print('Modular Custom source:', len(code), 'characters')
