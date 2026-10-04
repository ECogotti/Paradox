// Niagara seeds remain static; world-space advection is evaluated on the GPU.
float Clock = TimeOverride >= 0 ? TimeOverride : Time;
float S = Seed * 193.713 + 0.123;
float4 H = frac(sin(S * float4(17.31,39.71,83.17,117.13)) * float4(13758.13,43758.55,23147.31,97157.17));
float Motion = RotationSpeed;
float Phase = frac(H.y + Clock * abs(Motion) / max(Lifetime * 0.35, 0.01));
float T = Motion < 0 ? 1-Phase : Phase;
float3 Center, Tangent;
float Length, Width;
if (H.x < 0.40)
{
    float U = 1-T;
    Center = RouteStart*U*U*U + RouteA*3*U*U*T + RouteB*3*U*T*T + PlaneOrigin*T*T*T;
    Tangent = normalize((RouteA-RouteStart)*U*U + (RouteB-RouteA)*2*U*T + (PlaneOrigin-RouteB)*T*T + float3(0.001,0,0));
    float RouteLength = max(length(PlaneOrigin-RouteStart),LocalExtent.x);
    float3 Side = normalize(cross(PlaneUp,Tangent)+float3(0.001,0,0));
    Center += Side*(H.z-.5)*RouteLength*.045 + PlaneUp*(H.w-.5)*RouteLength*.008;
    Length = RouteLength * lerp(.10,.17,H.z);
    Width = RouteLength * lerp(.035,.055,H.w);
}
else
{
    float Angle = H.z * 6.2831853;
    float Radius = sqrt(H.w);
    float3 Reference = abs(PlaneUp.z) < .9 ? float3(0,0,1) : float3(0,1,0);
    float3 Right = normalize(cross(PlaneUp,Reference));
    float3 Up = normalize(cross(PlaneUp,Right));
    Center = PlaneOrigin + Right*cos(Angle)*Radius*LocalExtent.x + Up*sin(Angle)*Radius*LocalExtent.y;
    Center += PlaneUp*((H.y-.30)*LocalExtent.z*1.5);
    Tangent = normalize(cross(SourceAxis,Center-SourceCenter)+float3(.001,0,0));
    Tangent = normalize(Tangent-PlaneUp*dot(Tangent,PlaneUp)+float3(.001,0,0));
    Center += Tangent*(T-.5)*LocalExtent.x*.5;
    Center += PlaneUp*sin(Clock*.3+S)*LocalExtent.z*.1*Turbulence;
    Length = LocalExtent.x * lerp(.13,.22,H.z);
    Width = LocalExtent.y * lerp(.085,.14,H.w);
}
Length *= ParticleScale;
Width *= ParticleScale;
float2 Q = UV-.5;
float3 Offset;
if (Kind < .5)
{
    float A=H.z*6.2831853;
    float2 P=float2(Q.x*cos(A)-Q.y*sin(A),Q.x*sin(A)+Q.y*cos(A));
    Offset = CameraRight*P.x*Length*2.0 + CameraUp*P.y*Width*2.5;
}
else
{
    float3 Side = normalize(cross(CameraForward,Tangent)+CameraRight*.001);
    float KindLength = Kind < 1.5 ? Length*1.65 : Length*.065;
    float KindWidth = Kind < 1.5 ? Width*.045 : Width*.015;
    Offset = Tangent*Q.x*KindLength + Side*Q.y*KindWidth;
}
return Center + Offset - OriginalWorldRel;
