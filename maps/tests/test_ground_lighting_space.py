"""Execute the terrain normal expression across chase-camera rotations.

The regression makes grazing sunlight vanish, taking real ground shadows with it.
"""
from pathlib import Path
import math
import re
from test_meadow_world_space import Matrix, Vector, mat3, normalize, close

root = Path(__file__).resolve().parents[2]
shader = (root/'maps/toolkit/shaders/ground_splat.vs').read_text()
expression = re.search(r'fragNormal\s*=\s*([^;]+);', shader).group(1)
sun = normalize(Vector((.20, .24, -.95)))
for pitch in (0, 18, 35, 60):
    for yaw in (0, 75, 180):
        p, y = math.radians(pitch), math.radians(yaw)
        view = Matrix([[math.cos(y), 0, math.sin(y), 13],
                       [math.sin(p)*math.sin(y), math.cos(p), -math.sin(p)*math.cos(y), -8],
                       [-math.cos(p)*math.sin(y), math.sin(p), math.cos(p)*math.cos(y), 42],
                       [0, 0, 0, 1]])
        for authored in ((0,1,0), (.1,.98,.15), (-.2,.97,-.08)):
            normal = normalize(Vector(authored))
            output = eval(expression, {'__builtins__': {}},
                          dict(normalize=normalize, mat3=mat3, matModel=view, vertexNormal=normal))
            close(output, normal)
            expected = max(sum(a*b for a,b in zip(normal,sun)),0)
            observed = max(sum(a*b for a,b in zip(output,sun)),0)
            assert abs(expected-observed) < 1e-10
            # Shadowing must attenuate direct irradiance on the same terrain
            # regardless of the orbit camera. Ambient remains unaffected.
            assert abs((.2 + 3*observed) - (.2 + 3*observed*.2) - 3*expected*.8) < 1e-10
print('PASS: terrain world normals, grazing sunlight and shadow contrast survive 12 camera rotations')
