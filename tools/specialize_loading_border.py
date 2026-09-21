"""Border emulation for the loading mask's non-mipmapped stage-1 texture.

The runtime supplies an edge-clamped texture and captured state. Ordinary
programs are unchanged. Dimensions are positive for linear, negative for point.
"""

def specialize(source: str) -> str:
    sample = 'float4 t1 = tex2D(tex1, IN.texcoord1.xy);'
    if source.count(sample) != 1:
        raise ValueError('loading border requires one direct stage-1 2D sample')
    source = source.replace('uniform float4 xv_atest)',
        'uniform float4 xv_atest, uniform float4 xv_border1[2])')
    if 'uniform float4 xv_border1[2]' not in source:
        raise ValueError('loading fragment signature changed')
    return source.replace(sample, sample + '''
    // Exact base-level border contribution for point or bilinear sampling.
    float2 uv1 = IN.texcoord1.xy;
    float2 size1 = abs(xv_border1[1].xy);
    float2 linear1 = saturate(uv1 * size1 + 0.5) * saturate((1.0 - uv1) * size1 + 0.5);
    float2 point1 = step(0.0, uv1) * (1.0 - step(1.0, uv1));
    float2 inside1 = lerp(point1, linear1, step(0.0, xv_border1[1].xy));
    inside1 = lerp(float2(1.0, 1.0), inside1, xv_border1[1].zw);
    t1 = lerp(xv_border1[0], t1, inside1.x * inside1.y);''')
