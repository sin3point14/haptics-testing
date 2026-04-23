import numpy as np

with open('./out.txt', 'r', encoding='utf-16-le') as f:
    lines = f.readlines()


def parse_csv3(line):
    vals = [x.strip().lstrip('\ufeff') for x in line.split(',')]
    return np.array([float(x) for x in vals], dtype=float)


pos = []
rot = []

for i in range(0, len(lines) - 3, 4):
    p = parse_csv3(lines[i])
    r0 = parse_csv3(lines[i + 1])
    r1 = parse_csv3(lines[i + 2])
    r2 = parse_csv3(lines[i + 3])

    if p.size != 3 or r0.size != 3 or r1.size != 3 or r2.size != 3:
        continue

    pos.append(p)
    rot.append(np.vstack([r0, r1, r2]))

n = min(len(pos), len(rot))
if n % 2 == 1:
    n -= 1

pos = pos[:n]
rot = rot[:n]

indices = np.random.permutation(n)

half = n // 2
idx1 = indices[:half]
idx2 = indices[half:]

# split into two sets
pos_1 = [pos[i] for i in idx1]
rot_1 = [rot[i] for i in idx1]

pos_2 = [pos[i] for i in idx2]
rot_2 = [rot[i] for i in idx2]

A = []
b = []

for p1, r1, p2, r2 in zip(pos_1, rot_1, pos_2, rot_2):
    R1 = r1
    R2 = r2

    A.append(R2 - R1)
    b.append(p2 - p1)

A = np.vstack(A)
b = np.concatenate(b)

d = np.array([+3.91, 0, 0])
print("initial residual:", np.linalg.norm(A @ d - b))

d, r, *_ = np.linalg.lstsq(A, b, rcond=None)

print("Optimal displacement:", d)
print("Residual:", np.sqrt(r))