// FogSafe: Apply Fogging ON, Compute Fog Per Pixel ON, Allow Negative Emissive Color ON.
// The capture retains fog. Only its background contribution is inverse-fogged before
// the standard translucent pass applies the current view fog, avoiding a second layer.
// One Custom node, CMOT Float4. RGB -> Emissive Color, A -> Opacity.

// Position = (AbsoluteWorldPosition - ObjectPositionWS) / ObjectRadius.

// CameraVector points towards the camera; tracing proceeds away from it.

// Evaluate derivatives before the ray loop and divergent early exits.

float3 ScreenDx = ddx(Position);

float3 ScreenDy = ddy(Position);

float ViewLength2 = dot(CameraVector, CameraVector);

if (ViewLength2 < 1e-8)

    return float4(0, 0, 0, 0);



float3 RayOrigin = Position;

float3 RayDirection = -CameraVector * rsqrt(ViewLength2);

float AxisLength2 = dot(DiskAxis, DiskAxis);

float3 Axis = float3(0, 0, 1);

if (AxisLength2 > 1e-8)

    Axis = DiskAxis * rsqrt(AxisLength2);

float3 Reference = abs(Axis.z) < 0.95

    ? float3(0, 0, 1) : float3(1, 0, 0);

float3 BasisU = normalize(cross(Reference, Axis));

float3 BasisV = cross(Axis, BasisU);



float Core = clamp(CoreRadius, 0.02, 0.55);

float Inner = clamp(DiskInnerRadius, Core + 0.02, 0.85);

float Outer = clamp(DiskOuterRadius, Inner + 0.04, 0.95);

float Thickness = clamp(DiskThickness, 0.005, 0.20);

float Density = max(DiskDensity, 0.0);

float Emission = max(EmissionStrength, 0.0);

float HaloAmount = max(HaloStrength, 0.0);

float Lens = saturate(LensingStrength);

int Budget = (int)clamp(floor(RaySteps + 0.5), 64.0, 256.0);

float3 HotColor = max(InnerColor, float3(0, 0, 0));

float3 CoolColor = max(OuterColor, float3(0, 0, 0));

float DetailAmount = saturate(NoiseAmount);

float DetailScale = clamp(floor(NoiseScale + 0.5), 1.0, 32.0);

float Contrast = clamp(NoiseContrast, 0.25, 4.0);

float Spots = clamp(HotspotStrength, 0.0, 8.0);

float Evolution = clamp(TurbulenceSpeed, 0.0, 2.0);


float ReferenceRadius = 0.5 * (Inner + Outer);

float ReferenceOmega = RotationSpeed

    / max(pow(ReferenceRadius, 1.5), 0.15);



if (Evolution > 1e-5)

{

    float RotationQuantum = 6.28318531 * Evolution / DetailScale;

    ReferenceOmega = floor(ReferenceOmega / RotationQuantum + 0.5)

        * RotationQuantum;

}



float CycleA = frac(Time * Evolution);

float CycleB = frac(Time * Evolution + 0.5);

float AgeA = Evolution > 1e-5 ? CycleA / Evolution : 0.0;

float AgeB = Evolution > 1e-5 ? CycleB / Evolution : 0.0;

float WeightA = sin(3.14159265 * CycleA);

WeightA *= WeightA;

float WeightB = 1.0 - WeightA;



uint NoiseWidth, NoiseHeight;

DiskNoise.GetDimensions(NoiseWidth, NoiseHeight);

float2 NoiseSize = float2(NoiseWidth, NoiseHeight);

float LastMip = log2(max(max(NoiseSize.x, NoiseSize.y), 1.0));



// Intersect the proxy volume.

float B = dot(RayOrigin, RayDirection);

float OriginLength2 = dot(RayOrigin, RayOrigin);

float ProxyDiscriminant = B * B - OriginLength2 + 1.0;

if (ProxyDiscriminant <= 0.0)

    return float4(0, 0, 0, 0);



float ProxyRoot = sqrt(ProxyDiscriminant);

float Start = max(-B - ProxyRoot, 0.0);

float End = -B + ProxyRoot;

if (End <= Start)

    return float4(0, 0, 0, 0);



float3 Entry = RayOrigin + Start * RayDirection;

float EntryRadius = max(length(Entry), 1e-6);

float3 RadialBasis = Entry / EntryRadius;

float RadialDirection = dot(RayDirection, RadialBasis);

float3 Tangent = RayDirection - RadialDirection * RadialBasis;

float TangentLength = length(Tangent);

bool Straight = Lens <= 0.0 || TangentLength < 1e-5;

float3 TangentBasis = Tangent / max(TangentLength, 1e-5);



// Preserve the unbent exit before shortening the path against the core.

float3 StraightExit = RayOrigin + End * RayDirection;

bool ValidExit = false;

float3 ExitPosition = StraightExit;

float3 ExitDirection = RayDirection;

bool HitCore = EntryRadius <= Core;



if (Straight)

{

    float Discriminant = B * B - OriginLength2 + Core * Core;

    if (Discriminant > 0.0)

    {

        float Root = sqrt(Discriminant);

        float Near = -B - Root;

        float Far = -B + Root;

        if (Far >= Start && Near <= End)

        {

            End = max(Start, Near);

            HitCore = true;

        }

    }

}



if (Straight && !HitCore)

    ValidExit = true;



// Schwarzschild orbital coordinates: (u, du/dphi), u = Core/r.

float U0 = Core / EntryRadius;

float2 State = float2(

    U0, -U0 * RadialDirection / max(TangentLength, 1e-5));

float Phi = 0.0;

float StraightStep = (End - Start) / 64.0;

float TargetLength = 4.0 / (float)Budget;

float MaxAngle = 18.0 / (float)Budget;

float EdgeWidth = min(0.035, (Outer - Inner) * 0.25);

float3 PremultipliedColor = float3(0, 0, 0);

float Transmittance = 1.0;

int Count = Straight ? 64 : Budget;



[loop]

for (int Index = 0; Index < Count; ++Index)

{

    float3 SamplePosition;

    float StepLength;

    float3 SegmentVector;

    bool StopAfterSample = false;



    if (Straight)

    {

        SamplePosition = RayOrigin

            + RayDirection * (Start + (Index + 0.5) * StraightStep);

        StepLength = StraightStep;

        SegmentVector = RayDirection * StraightStep;

    }

    else

    {

        if (HitCore || State.x <= 0.0)

            break;



        float Speed = Core * length(State)

            / max(State.x * State.x, 1e-10);

        float H = min(MaxAngle, TargetLength / max(Speed, 1e-6));

        H = min(H, 0.08 * State.x / max(abs(State.y), 1e-6));

        if (H < 1e-8)

            break;



        float2 K1 = float2(

            State.y, -State.x + 1.5 * Lens * State.x * State.x);

        float2 Q = State + 0.5 * H * K1;

        float2 K2 = float2(

            Q.y, -Q.x + 1.5 * Lens * Q.x * Q.x);

        Q = State + 0.5 * H * K2;

        float2 K3 = float2(

            Q.y, -Q.x + 1.5 * Lens * Q.x * Q.x);

        Q = State + H * K3;

        float2 K4 = float2(

            Q.y, -Q.x + 1.5 * Lens * Q.x * Q.x);

        float2 Next = State + (H / 6.0)

            * (K1 + 2.0 * K2 + 2.0 * K3 + K4);



        // Detect a horizon crossing within a grazing integration step.

        float Energy = State.y * State.y

            + State.x * State.x * (1.0 - Lens * State.x);

        bool GrazingCapture = Lens < (2.0 / 3.0)

            && State.y > 0.0 && Next.y < 0.0

            && Energy >= 1.0 - Lens;



        if (GrazingCapture)

        {

            H *= saturate(State.y / max(State.y - Next.y, 1e-8));

            Q = State + 0.5 * H * K1;

            K2 = float2(Q.y, -Q.x + 1.5 * Lens * Q.x * Q.x);

            Q = State + 0.5 * H * K2;

            K3 = float2(Q.y, -Q.x + 1.5 * Lens * Q.x * Q.x);

            Q = State + H * K3;

            K4 = float2(Q.y, -Q.x + 1.5 * Lens * Q.x * Q.x);

            Next = State + (H / 6.0)

                * (K1 + 2.0 * K2 + 2.0 * K3 + K4);

        }



        bool Captured = Next.x >= 1.0 || GrazingCapture;

        bool Escaped = Next.y < 0.0 && Next.x <= Core;



        if (Captured || Escaped)

        {

            float BoundaryU = Captured ? 1.0 : Core;

            float Fraction = saturate((BoundaryU - State.x)

                / (abs(Next.x - State.x) > 1e-8

                    ? Next.x - State.x : 1e-8));

            H *= Fraction;



            Q = State + 0.5 * H * K1;

            K2 = float2(Q.y, -Q.x + 1.5 * Lens * Q.x * Q.x);

            Q = State + 0.5 * H * K2;

            K3 = float2(Q.y, -Q.x + 1.5 * Lens * Q.x * Q.x);

            Q = State + H * K3;

            K4 = float2(Q.y, -Q.x + 1.5 * Lens * Q.x * Q.x);

            Next = State + (H / 6.0)

                * (K1 + 2.0 * K2 + 2.0 * K3 + K4);

            Next.x = BoundaryU;



            HitCore = Captured;

            StopAfterSample = true;

        }



        float2 Mid = State + H * ((5.0 / 24.0) * K1

            + (1.0 / 6.0) * (K2 + K3) - (1.0 / 24.0) * K4);

        float SinPhi, CosPhi;

        sincos(Phi + 0.5 * H, SinPhi, CosPhi);

        SamplePosition = (Core / max(Mid.x, Core))

            * (CosPhi * RadialBasis + SinPhi * TangentBasis);

        float3 PreviousPosition = (Core / max(State.x, Core))

            * (cos(Phi) * RadialBasis + sin(Phi) * TangentBasis);



        Phi += H;



        float3 NextPosition = (Core / max(Next.x, Core))

            * (cos(Phi) * RadialBasis + sin(Phi) * TangentBasis);

        StepLength = length(SamplePosition - PreviousPosition)

            + length(NextPosition - SamplePosition);

        SegmentVector = NextPosition - PreviousPosition;

        State = Next;



        if (Escaped && !Captured)

        {

            float3 ExitRadial = cos(Phi) * RadialBasis

                + sin(Phi) * TangentBasis;

            float3 ExitTangent = -sin(Phi) * RadialBasis

                + cos(Phi) * TangentBasis;

            float3 Direction = -Next.y * ExitRadial

                + Next.x * ExitTangent;

            float Length2 = dot(Direction, Direction);

            if (Length2 > 1e-10)

            {

                ExitPosition = NextPosition;

                ExitDirection = Direction * rsqrt(Length2);

                ValidExit = true;

            }

        }

    }



    float Height = dot(SamplePosition, Axis);

    float3 PlanarPosition = SamplePosition - Axis * Height;

    float Radius = length(PlanarPosition);

    float RadialMask = smoothstep(Inner, Inner + EdgeWidth, Radius)

        * (1.0 - smoothstep(Outer - EdgeWidth, Outer, Radius));



    if (RadialMask > 0.0 && abs(Height) <= Thickness * 3.0)

    {

        float VerticalMask = exp(

            -Height * Height / (Thickness * Thickness));

        float Angle = atan2(

            dot(PlanarPosition, BasisV), dot(PlanarPosition, BasisU));

        float Phase = Angle - Time * RotationSpeed

            / max(pow(Radius, 1.5), 0.15);

        float Spiral = 0.55 + 0.45

            * sin(6.0 * Phase + 36.0 * Radius);

        float Bands = 0.65 + 0.35

            * sin(90.0 * Radius + 4.0 * sin(3.0 * Phase));

        float Structure = 0.25 + 0.75 * Spiral * Bands;

        float RadialFraction = saturate(

            (Radius - Inner) / (Outer - Inner));

        float Heat = (1.0 - RadialFraction)

            * (1.0 - RadialFraction);

        float3 SampleEmission = lerp(CoolColor, HotColor, Heat)
            * Emission * (0.65 + 1.20 * Heat);

        if (DetailAmount > 0.0)

        {

            float Omega = RotationSpeed

                / max(pow(Radius, 1.5), 0.15);

            float AngleA = Angle - Time * ReferenceOmega

                - (Omega - ReferenceOmega) * AgeA;

            float AngleB = Angle - Time * ReferenceOmega

                - (Omega - ReferenceOmega) * AgeB;

            float2 UVA = float2(

                AngleA * (DetailScale / 6.28318531),

                RadialFraction * (0.5 * DetailScale) + 0.08 * AgeA);

            float2 UVB = float2(

                AngleB * (DetailScale / 6.28318531),

                RadialFraction * (0.5 * DetailScale) + 0.08 * AgeB);



            float3 RadialUnit = PlanarPosition / max(Radius, 1e-5);

            float3 AngularUnit = cross(Axis, RadialUnit);

            float RadialFoot = max(

                max(abs(dot(ScreenDx, RadialUnit)),

                    abs(dot(ScreenDy, RadialUnit))),

                0.5 * abs(dot(SegmentVector, RadialUnit)));

            float AngularFoot = max(

                max(abs(dot(ScreenDx, AngularUnit)),

                    abs(dot(ScreenDy, AngularUnit))),

                0.5 * abs(dot(SegmentVector, AngularUnit)))

                / max(Radius, 1e-5);

            float OmegaSlope = pow(Radius, 1.5) > 0.15

                ? 1.5 * abs(RotationSpeed)

                    / max(pow(Radius, 2.5), 1e-5)

                : 0.0;



            float2 FootprintA = float2(

                (AngularFoot + OmegaSlope * AgeA * RadialFoot)

                    * (DetailScale / 6.28318531),

                RadialFoot * (0.5 * DetailScale) / (Outer - Inner));

            float2 FootprintB = float2(

                (AngularFoot + OmegaSlope * AgeB * RadialFoot)

                    * (DetailScale / 6.28318531),

                RadialFoot * (0.5 * DetailScale) / (Outer - Inner));

            float2 TexelFootA = FootprintA * NoiseSize;

            float2 TexelFootB = FootprintB * NoiseSize;

            float MipA = clamp(

                log2(max(max(TexelFootA.x, TexelFootA.y), 1.0))

                    - 0.5, 0.0, LastMip);

            float MipB = clamp(

                log2(max(max(TexelFootB.x, TexelFootB.y), 1.0))

                    - 0.5, 0.0, LastMip);



            float3 PatternA = Texture2DSampleLevel(

                DiskNoise, DiskNoiseSampler, UVA, MipA).rgb;

            float3 PatternB = Texture2DSampleLevel(

                DiskNoise, DiskNoiseSampler, UVB, MipB).rgb;

            float3 Pattern = WeightA * PatternA + WeightB * PatternB;

            float Grain = pow(max(Pattern.g, 0.001), Contrast);

            float Gain = (0.30 + 1.40 * Pattern.r)

                * (0.08 + 2.80 * Grain) + 2.0 * Spots * Pattern.b;



            SampleEmission *= lerp(1.0, Gain, DetailAmount);

            Structure = lerp(

                Structure,

                0.518125 * (0.70 + 0.60 * Pattern.r),

                DetailAmount);

        }



        float SampleOpacity = 1.0 - exp(

            -Density * RadialMask * VerticalMask

                * Structure * StepLength);

        PremultipliedColor += Transmittance

            * SampleOpacity * SampleEmission;

        Transmittance *= 1.0 - SampleOpacity;

    }



    if (StopAfterSample || (Straight && Transmittance < 0.005))

        break;

}



float Opacity = HitCore ? 1.0 : 1.0 - Transmittance;



if (!HitCore && HaloAmount > 0.0)

{

    float Impact = length(

        RayOrigin + RayDirection * max(-B, 0.0));

    float ShadowRadius = Core;



    if (Lens > 0.0)

    {

        float PeakU = clamp(

            2.0 / max(3.0 * Lens, 1e-6), Core, 1.0);

        float Peak = PeakU * PeakU * (1.0 - Lens * PeakU);

        ShadowRadius = Core / sqrt(

            max(Peak + Lens * Core * Core * Core, 1e-6));

    }



    float RingRadius = Straight && Lens <= 0.0

        ? Core * 1.12 : ShadowRadius * 1.025;

    float RingWidth = max(ShadowRadius * 0.045, 0.008);

    float Offset = (Impact - RingRadius) / RingWidth;

    float CoreGate = smoothstep(

        ShadowRadius, ShadowRadius + 0.015, Impact);

    float ProxyFade = 1.0 - smoothstep(0.80, 0.95, Impact);

    float Ring = exp(-Offset * Offset) * CoreGate * ProxyFade;

    float Halo = exp(-max(Impact - RingRadius, 0.0) * 10.0)

        * CoreGate * ProxyFade;

    float GlowOpacity = saturate(

        HaloAmount * (0.50 * Ring + 0.12 * Halo));

    float3 GlowEmission = HotColor * Emission * HaloAmount

        * (1.20 * Ring + 0.10 * Halo);



    PremultipliedColor += Transmittance * GlowEmission;

    Opacity += Transmittance * GlowOpacity;

}



// Background capture excludes this actor and stores linear HDR radiance.

float Refraction = saturate(RefractionStrength);

if (Refraction > 0.0 && BackgroundReady > 0.5

    && ValidExit && !HitCore)

{

    float Impact = length(

        RayOrigin + RayDirection * max(-B, 0.0));

    float Coverage = (1.0 - smoothstep(0.80, 0.95, Impact))

        * smoothstep(0.0, 0.05, Refraction);

    // Inverse the proxy fog on the already-fogged background only.
    float4 ProxyFog = float4(0, 0, 0, 1);
#if PIXELSHADER && defined(SceneColorCopyTexture) && SHADING_PATH_DEFERRED && MATERIAL_ENABLE_TRANSLUCENCY_FOGGING && MATERIAL_COMPUTE_FOG_PER_PIXEL
    half4 CalculateHeightFog(float3 WorldPositionRelativeToCamera, uint EyeIndex, ViewState InView);
    float3 ComputeVolumeUV(FDFVector3 WorldPosition, FDFInverseMatrix WorldToClip, ViewState InView);
    float4 CombineVolumetricFog(float4 GlobalFog, float3 VolumeUV, uint EyeIndex, float SceneDepth, ViewState InView);
    ProxyFog = CalculateHeightFog(Parameters.WorldPosition_CamRelative, 0, ResolvedView);
    if (TranslucentBasePass.Shared.Fog.ApplyVolumetricFog > 0)
        ProxyFog = CombineVolumetricFog(ProxyFog,
            ComputeVolumeUV(Parameters.AbsoluteWorldPosition, ResolvedView.WorldToClip, ResolvedView),
            0, PixelDepth, ResolvedView);
#endif
    // Fully opaque fog hides refraction; retain the existing scene instead of dividing by zero.
    Coverage *= smoothstep(0.01, 0.03, ProxyFog.a);
    bool Perspective = ResolvedView.ViewToClip[3][3] < 0.5;

    float2 ProjectionScale = abs(float2(

        ResolvedView.ViewToClip[0][0],

        ResolvedView.ViewToClip[1][1]));

    float2 ProjectedRadius;

    float2 OffsetUV = float2(0, 0);

    bool ProjectionValid = true;



    if (Perspective)

    {

        float4 OriginalClip = mul(

            float4(TransformWorldVectorToView(RayDirection), 0),

            ResolvedView.ViewToClip);

        float4 ExitClip = mul(

            float4(TransformWorldVectorToView(ExitDirection), 0),

            ResolvedView.ViewToClip);



        ProjectionValid = OriginalClip.w > 1e-5

            && ExitClip.w > 1e-5;

        if (ProjectionValid)

            OffsetUV = 0.5 * float2(1, -1)

                * (ExitClip.xy / ExitClip.w

                    - OriginalClip.xy / OriginalClip.w);



        float Distance2 = dot(CameraPosition, CameraPosition);

        ProjectedRadius = 0.5 * ProjectionScale

            / sqrt(max(Distance2 - 1.0, 1e-4));

    }

    else

    {

        float Extension = clamp(

            OrthoRefractionDistance, 0.0, 100.0);

        float3 Delta = (ExitPosition - StraightExit

            + Extension * (ExitDirection - RayDirection))

            * max(SphereRadiusWS, 1e-3);

        float4 DeltaClip = mul(

            float4(TransformWorldVectorToView(Delta), 0),

            ResolvedView.ViewToClip);



        OffsetUV = 0.5 * float2(1, -1) * DeltaClip.xy;

        ProjectedRadius = 0.5 * ProjectionScale

            * max(SphereRadiusWS, 1e-3);

    }



    OffsetUV *= Refraction;

    float2 ScaledOffset = OffsetUV

        / max(ProjectedRadius, float2(1e-6, 1e-6));

    float OffsetLength = length(ScaledOffset);

    float Maximum = clamp(RefractionMaxOffset, 0.0, 1.0);

    OffsetUV *= min(1.0, Maximum / max(OffsetLength, 1e-6));

    OffsetUV *= 1.0 - smoothstep(0.80, 0.95, Impact);

    float2 WarpedUV = ScreenUV + OffsetUV;



    uint BackgroundWidth, BackgroundHeight;

    BackgroundTex.GetDimensions(BackgroundWidth, BackgroundHeight);

    float2 HalfTexel = 0.5 / max(

        float2(BackgroundWidth, BackgroundHeight), float2(1, 1));

    bool Inside = all(WarpedUV >= HalfTexel)

        && all(WarpedUV <= 1.0 - HalfTexel);



    if (ProjectionValid && Inside && Coverage > 0.0)

    {

        bool Foreground = false;



#if !SCENE_TEXTURES_DISABLED && !PATH_TRACING

        float WarpedDeviceZ = LookupDeviceZ(

            ViewportUVToBufferUV(WarpedUV));

        float CurrentDeviceZ = LookupDeviceZ(

            ViewportUVToBufferUV(ScreenUV));

        float WarpedDepth = ConvertFromDeviceZ(WarpedDeviceZ);

        float DepthTolerance = max(1.0, abs(PixelDepth) * 1e-5);



        // Reversed-Z clear value zero represents the sky.

        Foreground = (WarpedDeviceZ > 0.0

                && WarpedDepth < PixelDepth - DepthTolerance)

            || (CurrentDeviceZ > 0.0

                && SceneDepthDependency < PixelDepth - DepthTolerance);

#endif



        if (!Foreground)

        {

            float3 Background = max(

                Texture2DSampleLevel(

                    BackgroundTex, BackgroundTexSampler,

                    WarpedUV, 0).rgb,

                float3(0, 0, 0));

            Background = (Background - ProxyFog.rgb) / max(ProxyFog.a, 0.01);
            float Remaining = 1.0 - saturate(Opacity);

            PremultipliedColor += Remaining * Coverage * Background;

            Opacity += Remaining * Coverage;

        }

    }

}



Opacity = saturate(Opacity);

if (Opacity < 1e-5)

    return float4(0, 0, 0, 0);



return float4(PremultipliedColor / Opacity, Opacity);