#version 120
uniform sampler2D edgeTexture;
uniform vec2 inverseSceneSize;

void main()
{
    vec2 uv = gl_FragCoord.xy * inverseSceneSize;
    vec2 e = texture2D(edgeTexture, uv).rg;
    float left = 0.0, right = 0.0, up = 0.0, down = 0.0;
    float crossLeft = 0.0, crossRight = 0.0, crossUp = 0.0, crossDown = 0.0;
    vec2 lo = vec2(0.0), hi = vec2(1.0);

    if (e.x > 0.0)
    {
        for (int i = 1; i <= 12; ++i)
        {
            vec2 p = clamp(uv - vec2(inverseSceneSize.x * float(i), 0.0), lo, hi);
            vec2 s = texture2D(edgeTexture, p).rg;
            if (s.r < 0.5) { crossLeft = s.g; break; }
            left += 1.0;
            crossLeft = max(crossLeft, s.g);
        }
        for (int i = 1; i <= 12; ++i)
        {
            vec2 p = clamp(uv + vec2(inverseSceneSize.x * float(i), 0.0), lo, hi);
            vec2 s = texture2D(edgeTexture, p).rg;
            if (s.r < 0.5) { crossRight = s.g; break; }
            right += 1.0;
            crossRight = max(crossRight, s.g);
        }
    }

    if (e.y > 0.0)
    {
        for (int i = 1; i <= 12; ++i)
        {
            vec2 p = clamp(uv - vec2(0.0, inverseSceneSize.y * float(i)), lo, hi);
            vec2 s = texture2D(edgeTexture, p).rg;
            if (s.g < 0.5) { crossDown = s.r; break; }
            down += 1.0;
            crossDown = max(crossDown, s.r);
        }
        for (int i = 1; i <= 12; ++i)
        {
            vec2 p = clamp(uv + vec2(0.0, inverseSceneSize.y * float(i)), lo, hi);
            vec2 s = texture2D(edgeTexture, p).rg;
            if (s.g < 0.5) { crossUp = s.r; break; }
            up += 1.0;
            crossUp = max(crossUp, s.r);
        }
    }

    // Analytic area approximation. Crossing edges reduce the blend at corners,
    // preserving small glyphs/foliage better than the old uniform 0.72 weights.
    float hsum = max(left + right + 1.0, 1.0);
    float vsum = max(up + down + 1.0, 1.0);
    float hCorner = 1.0 - 0.30 * clamp(crossLeft + crossRight, 0.0, 1.0);
    float vCorner = 1.0 - 0.30 * clamp(crossUp + crossDown, 0.0, 1.0);
    vec4 w = vec4((right + 0.5) / hsum, (left + 0.5) / hsum,
                  (up + 0.5) / vsum, (down + 0.5) / vsum);
    w.xy *= e.x * hCorner * 0.70;
    w.zw *= e.y * vCorner * 0.70;
    gl_FragColor = clamp(w, 0.0, 0.92);
}
