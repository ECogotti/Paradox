float Clock = TimeOverride >= 0 ? TimeOverride : Time;
float S = Seed * 193.713 + .123;
float4 H=frac(sin(S*float4(17.31,39.71,83.17,117.13))*float4(13758.13,43758.55,23147.31,97157.17));
float Phase=frac(H.y+Clock*abs(RotationSpeed)/max(Lifetime*.35,.01));
float Envelope=pow(saturate(sin(Phase*3.14159265)),.8);
// Camera depth relative to the gameplay-plane anchor. An infinite plane-ray
// intersection would misclassify elevated gas at the distant source as foreground.
float DepthAfterPlane=dot(WorldRel-PlaneOrigin,CameraForward);
float Middle=smoothstep(-LayerTransition,LayerTransition,DepthAfterPlane);
float Partition=Layer>.5?1-Middle:Middle;
float Intensity=Layer>.5?ForegroundIntensity:MidgroundIntensity;
float NearFade=smoothstep(40,220,length(WorldRel-CameraRel));
float Radius=length(WorldRel-SourceCenter)/max(SourceRadius,1);
float NearSource=.35+.65/(1+Radius*.4);
float3 Color=lerp(InnerColor,OuterColor,saturate((Radius-.18)/1.5));
float Alpha=0, Light=0;
if (Kind < .5)
{
    float Frame=frac(Clock*.085+H.w)*16;
    float F0=floor(Frame),F1=fmod(F0+1,16);
    float2 A=(float2(fmod(F0,4),floor(F0/4))+.065+UV*.87)/4;
    float2 B=(float2(fmod(F1,4),floor(F1/4))+.065+UV*.87)/4;
    float4 Noise=lerp(Texture2DSampleLevel(GasTex,GasTexSampler,A,1),Texture2DSampleLevel(GasTex,GasTexSampler,B,1),frac(Frame));
    float Edge = smoothstep(0,.16,UV.x)*smoothstep(0,.16,1-UV.x)*smoothstep(0,.16,UV.y)*smoothstep(0,.16,1-UV.y);
    float2 CloudRadius=(UV-.5)*2;
    Edge *= pow(saturate(1-dot(CloudRadius,CloudRadius)),2);
    Alpha=sqrt(saturate(Noise.a))*Edge*GasOpacity;
    Light=.6+Noise.r*.8;
}
else if (Kind < 1.5)
{
    float V=UV.y-.5 + .19*sin(UV.x*6.2831853+S+Clock*.2)*Turbulence;
    float Line=exp(-V*V*50)*pow(saturate(sin(UV.x*3.14159265)),1.5);
    float2 Polar=float2(H.z*2+UV.x*.5-Clock*RotationSpeed*.02,H.w+UV.y*.05);
    float4 Field=Texture2DSampleLevel(DiskStructure,DiskStructureSampler,Polar,2);
    Alpha=Line*(.12+.20*Field.b);
    Light=1.4+Field.a*1.6;
}
else
{
    float2 Q=(UV-.5)*float2(.8,2.0);
    Alpha=exp(-dot(Q,Q)*18)*pow(saturate(sin(UV.x*3.14159265)),.6)*.7;
    Light=2.0+H.z*1.5;
}
Alpha*=Envelope*Partition*Intensity*NearFade*NearSource;
return float4(Color*Light*Brightness, saturate(Alpha));
