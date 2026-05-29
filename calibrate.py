import argparse
import ctypes
import itertools
import os
import sys
import time

import numpy as np
import matplotlib.pyplot as plt
import trimesh
from mpl_toolkits.mplot3d.art3d import Poly3DCollection


mesh = trimesh.load("base_skull.stl")
vertices = mesh.vertices
faces = mesh.faces

queries_vertices = [
    (262439, 0, 1.0), # index 1, index 2, interpolation factor
    (262499, 0, 1.0),
    (1814, 0, 1.0),
    (1814, 1864, 1.0 - (61.389 / 160.3)),
    (1814, 262384, 1.0 - (51.82 / 264.898)),
    (1814, 262384, 1.0 - (91.07 / 264.898)),
    (1814, 262384, (104.6 / 264.898)),
    (262423, 0, 1.0),
    (262424, 0, 1.0),
    (262422, 0, 1.0),
]


RECORD_COUNT = len(queries_vertices)


def target_point_for_query(query_index: int) -> np.ndarray:
    v1, v2, alpha = queries_vertices[query_index]
    return alpha * np.asarray(vertices[v1], dtype=float) + (1.0 - alpha) * np.asarray(vertices[v2], dtype=float)


def load_recording(path: str) -> list[dict]:
    if not os.path.exists(path):
        return []
    import json
    with open(path, 'r', encoding='utf-8') as f:
        data = json.load(f)
    return data if isinstance(data, list) else []


def save_recording(path: str, samples: list[dict]) -> None:
    import json
    tmp_path = path + ".tmp"
    with open(tmp_path, 'w', encoding='utf-8') as f:
        json.dump(samples, f, indent=2)
    os.replace(tmp_path, path)


def add_mesh_to_axes(ax) -> None:
    face_count = len(mesh.faces)
    if face_count == 0:
        return

    step = max(1, face_count // 8000)
    sampled_faces = mesh.faces[::step]
    face_vertices = mesh.vertices[sampled_faces]
    collection = Poly3DCollection(face_vertices, facecolors=(0.75, 0.78, 0.82, 0.12), edgecolors=(0.35, 0.38, 0.42, 0.12), linewidths=0.15)
    ax.add_collection3d(collection)


def format_axis_permutation(perm: tuple[int, int, int]) -> str:
    axis_names = ["x", "y", "z"]
    return "(" + ", ".join(axis_names[index] for index in perm) + ")"


def build_axis_transform(perm: tuple[int, int, int], signs: tuple[int, int, int]) -> np.ndarray:
    transform = np.zeros((3, 3), dtype=float)
    for row, (source_axis, sign) in enumerate(zip(perm, signs)):
        transform[row, source_axis] = float(sign)
    return transform


def format_axis_transform(perm: tuple[int, int, int], signs: tuple[int, int, int]) -> str:
    axis_names = ["x", "y", "z"]
    parts = []
    for source_axis, sign in zip(perm, signs):
        prefix = "-" if sign < 0 else ""
        parts.append(prefix + axis_names[source_axis])
    return "(" + ", ".join(parts) + ")"


def transform_pose_sample(sample: dict, transform: np.ndarray) -> dict:
    p = np.asarray(sample["position"], dtype=float)
    r = np.asarray(sample["rotation"], dtype=float)
    permuted_position = transform @ p
    permuted_rotation = transform @ r
    return {
        "position": permuted_position.tolist(),
        "rotation": permuted_rotation.tolist(),
    }


def print_point_error_report(target_points: list[np.ndarray], predicted_points: list[np.ndarray]) -> None:
    for index, (target, predicted) in enumerate(zip(target_points, predicted_points), start=1):
        delta = predicted - target
        error_norm = float(np.linalg.norm(delta))
        print(
            f"point {index}: target=({target[0]:.3f}, {target[1]:.3f}, {target[2]:.3f}) "
            f"predicted=({predicted[0]:.3f}, {predicted[1]:.3f}, {predicted[2]:.3f}) "
            f"delta=({delta[0]:.3f}, {delta[1]:.3f}, {delta[2]:.3f}) norm={error_norm:.3f}"
        )


def plot_point_comparison(target_points: list[np.ndarray], predicted_points: list[np.ndarray], title: str | None = None) -> None:
    fig = plt.figure(figsize=(10, 8))
    ax = fig.add_subplot(111, projection="3d")

    target = np.asarray(target_points, dtype=float)
    predicted = np.asarray(predicted_points, dtype=float)

    add_mesh_to_axes(ax)

    ax.scatter(target[:, 0], target[:, 1], target[:, 2], c="tab:blue", marker="o", s=55, label="Interpolated targets")
    ax.scatter(predicted[:, 0], predicted[:, 1], predicted[:, 2], c="tab:orange", marker="^", s=55, label="s*(R*d + p)")
    ax.scatter(predicted[:, 0], predicted[:, 1], predicted[:, 2], c="tab:orange", marker="^", s=55, label="s*(R*d + p) + o")

    for i in range(len(target_points)):
        ax.plot(
            [target[i, 0], predicted[i, 0]],
            [target[i, 1], predicted[i, 1]],
            [target[i, 2], predicted[i, 2]],
            c="0.65",
            linewidth=1.0,
            alpha=0.7,
        )

    all_points = np.vstack([target, predicted])
    mins = all_points.min(axis=0)
    maxs = all_points.max(axis=0)
    center = (mins + maxs) * 0.5
    span = float(np.max(maxs - mins))
    half = span * 0.5 if span > 0 else 1.0

    ax.set_xlim(center[0] - half, center[0] + half)
    ax.set_ylim(center[1] - half, center[1] + half)
    ax.set_zlim(center[2] - half, center[2] + half)
    ax.set_xlabel("X")
    ax.set_ylabel("Y")
    ax.set_zlabel("Z")
    ax.set_title(title or "Interpolated targets vs fitted device points")
    ax.legend(loc="best")
    plt.tight_layout()
    plt.show()


def solve_local_vector(samples: list[dict]) -> tuple[np.ndarray, float, float, float, float, np.ndarray]:
    if len(samples) < RECORD_COUNT:
        raise ValueError(f"Need {RECORD_COUNT} samples, got {len(samples)}")

    # We solve for x = [u(3), o(3), s(1)] where u = s * d and o is the
    # global offset. For each sample:
    #   t_i = R_i u + o + s * p_i
    # which is linear in the unknowns.
    design_rows = []
    targets = []
    p_rows = []
    for i in range(RECORD_COUNT):
        sample = samples[i]
        p = np.asarray(sample["position"], dtype=float)
        r = np.asarray(sample["rotation"], dtype=float)
        target = target_point_for_query(i)
        # A_i = [R_i | I_3 | p_i]
        a_block = np.hstack([r, np.eye(3), p.reshape(3, 1)])
        design_rows.append(a_block)
        targets.append(target)
        p_rows.append(p)

    a_mat = np.vstack(design_rows)
    b_vec = np.concatenate(targets)
    # initial residual assuming d = 0, s = 1, o = 0 (i.e. predict p)
    initial_residual = float(np.linalg.norm(b_vec - np.concatenate(p_rows)))

    x, residuals, rank, singular_values = np.linalg.lstsq(a_mat, b_vec, rcond=None)
    residual = float(np.linalg.norm(a_mat @ x - b_vec))

    u = x[0:3]
    o = x[3:6]
    scale = float(x[6])
    if abs(scale) < 1e-12:
        raise ValueError("Estimated scale is too close to zero")
    d = u / scale
    return d, scale, o, initial_residual, residual, singular_values


def solve_local_vector_with_axis_permutation(samples: list[dict]) -> tuple[np.ndarray, float, np.ndarray, float, float, np.ndarray, tuple[int, int, int], tuple[int, int, int]]:
    best_result = None
    best_perm = None
    best_signs = None
    best_residual = None

    for perm in itertools.permutations((0, 1, 2)):
        for signs in itertools.product((1, -1), repeat=3):
            transform = build_axis_transform(perm, signs)
            transformed_samples = [transform_pose_sample(sample, transform) for sample in samples]
            result = solve_local_vector(transformed_samples)
            # result = (d, scale, o, initial_residual, residual, singular_values)
            residual = result[4]
            if best_result is None or residual < best_residual:
                best_result = result
                best_perm = perm
                best_signs = signs
                best_residual = residual

    if best_result is None or best_perm is None or best_signs is None:
        raise ValueError("No axis permutation produced a valid fit")

    return (*best_result, best_perm, best_signs)


def solve_local_vector_from_plane(samples: list[dict]) -> tuple[np.ndarray, float, float, float, np.ndarray]:
    if not samples:
        raise ValueError("Need at least one sample")

    design_rows = []
    p_ys = []
    for sample in samples:
        p = np.asarray(sample["position"], dtype=float)
        r = np.asarray(sample["rotation"], dtype=float)
        design_rows.append([r[1, 0], r[1, 1], r[1, 2]])
        p_ys.append(p[1])

    a_mat = np.asarray(design_rows, dtype=float)
    p_y_vec = np.asarray(p_ys, dtype=float)

    # Solve for d by forcing the y-values of R*d + p to be as equal as possible.
    # This is equivalent to fitting the centered system:
    #   (A - mean(A)) d = -(p_y - mean(p_y))
    a_centered = a_mat - np.mean(a_mat, axis=0, keepdims=True)
    p_centered = p_y_vec - float(np.mean(p_y_vec))
    initial_residual = float(np.linalg.norm(p_centered))

    d, residuals, rank, singular_values = np.linalg.lstsq(a_centered, -p_centered, rcond=None)
    residual = float(np.linalg.norm(a_centered @ d + p_centered))
    y_values = a_mat @ d + p_y_vec
    plane_y = float(np.mean(y_values))
    return d, plane_y, initial_residual, residual, singular_values


def load_library(base_dir: str) -> ctypes.CDLL:
    dll_name = "haptics_pose.dll" if os.name == "nt" else "libhaptics_pose.so"
    candidates = [
        os.path.join(base_dir, dll_name),
        os.path.join(base_dir, "build", dll_name),
        os.path.join(base_dir, "build", "Release", dll_name),
    ]
    for dll_path in candidates:
        if os.path.exists(dll_path):
            return ctypes.CDLL(dll_path)
    raise FileNotFoundError("; ".join(candidates))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--backend", default="openhaptics", choices=["openhaptics", "geomagic", "haption"])
    parser.add_argument("--device", default="", help="Device name for OpenHaptics or connection string for Haption")
    parser.add_argument("--interval", type=float, default=0.02, help="Seconds between queries")
    parser.add_argument("--samples", type=int, default=0, help="Number of samples to print before exiting; 0 means run forever")
    parser.add_argument("--output", default="recording.json", help="Output path for recording (JSON)")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--debug", action="store_true", help="Print pose every frame")
    mode.add_argument("--estimate-from-plane", action="store_true", help="Estimate d from samples constrained to a fixed y plane")
    mode.add_argument("--estimate-from-points", action="store_true", help="Record 10 query points and estimate d from them")
    args = parser.parse_args()

    base_dir = os.path.dirname(os.path.abspath(__file__))

    lib = load_library(base_dir)

    lib.haptics_open.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    lib.haptics_open.restype = ctypes.c_int
    lib.haptics_get_pose.argtypes = [ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_double)]
    lib.haptics_get_pose.restype = ctypes.c_int
    lib.haptics_close.argtypes = []
    lib.haptics_close.restype = None
    lib.haptics_last_error.argtypes = []
    lib.haptics_last_error.restype = ctypes.c_char_p

    backend = args.backend.encode("utf-8")
    device = args.device.encode("utf-8")
    if lib.haptics_open(backend, device) == 0:
        err = lib.haptics_last_error()
        raise RuntimeError(err.decode("utf-8") if err else "failed to open haptics backend")

    transform = (ctypes.c_double * 16)()
    position = (ctypes.c_double * 3)()
    try:
        if args.debug:
            printed = 0
            while True:
                if lib.haptics_get_pose(transform, position) == 0:
                    err = lib.haptics_last_error()
                    raise RuntimeError(err.decode("utf-8") if err else "failed to query pose")

                print(f"{position[0]:.6f},{position[1]:.6f},{position[2]:.6f}")
                print(f"{transform[0]:.6f},{transform[4]:.6f},{transform[8]:.6f}")
                print(f"{transform[1]:.6f},{transform[5]:.6f},{transform[9]:.6f}")
                print(f"{transform[2]:.6f},{transform[6]:.6f},{transform[10]:.6f}")
                sys.stdout.flush()
                printed += 1
                if args.samples > 0 and printed >= args.samples:
                    break
                time.sleep(args.interval)

            return 0

        if args.estimate_from_plane:
            print("Warning: plane mode does not estimate scale s; results assume the device and model units are already aligned.", file=sys.stderr)
            input("Press Enter when ready to start the 1 second wait... ")
            time.sleep(1.0)
            samples = []
            deadline = time.time() + 5.0
            while time.time() < deadline:
                if lib.haptics_get_pose(transform, position) == 0:
                    err = lib.haptics_last_error()
                    raise RuntimeError(err.decode("utf-8") if err else "failed to query pose")

                samples.append({
                    "position": [float(position[0]), float(position[1]), float(position[2])],
                    "rotation": [
                        [float(transform[0]), float(transform[4]), float(transform[8])],
                        [float(transform[1]), float(transform[5]), float(transform[9])],
                        [float(transform[2]), float(transform[6]), float(transform[10])],
                    ],
                })
                time.sleep(args.interval)

            d, plane_y, initial_residual, residual, singular_values = solve_local_vector_from_plane(samples)
            print(f"d = {d[0]:.9f}, {d[1]:.9f}, {d[2]:.9f}")
            print(f"plane_y = {plane_y:.9f}")
            print(f"initial_residual = {initial_residual:.9f}")
            print(f"residual_norm = {residual:.9f}")
            print("singular_values = " + ", ".join(f"{value:.9f}" for value in singular_values))
            return 0

        if args.estimate_from_points:
            out_path = os.path.join(base_dir, args.output)
            existing = load_recording(out_path)
            if len(existing) >= RECORD_COUNT:
                existing = existing[:RECORD_COUNT]
                d, scale, o, initial_residual, residual, singular_values, perm, signs = solve_local_vector_with_axis_permutation(existing)
                target_points = [target_point_for_query(i) for i in range(RECORD_COUNT)]
                predicted_points = []
                transform = build_axis_transform(perm, signs)
                for sample in [transform_pose_sample(sample, transform) for sample in existing]:
                    p = np.asarray(sample["position"], dtype=float)
                    r = np.asarray(sample["rotation"], dtype=float)
                    predicted_points.append(scale * (r @ d + p) + o)
                print(f"d = {d[0]:.9f}, {d[1]:.9f}, {d[2]:.9f}")
                print(f"scale = {scale:.9f}")
                print(f"offset = {o[0]:.9f}, {o[1]:.9f}, {o[2]:.9f}")
                print(f"axis_transform = {format_axis_transform(perm, signs)}")
                print(f"initial_residual = {initial_residual:.9f}")
                print(f"residual_norm = {residual:.9f}")
                print("singular_values = " + ", ".join(f"{value:.9f}" for value in singular_values))
                print_point_error_report(target_points, predicted_points)
                plot_point_comparison(target_points, predicted_points, title=f"Interpolated targets vs fitted device points {format_axis_transform(perm, signs)}")
                return 0

            start_step = len(existing) + 1
            for step in range(start_step, RECORD_COUNT + 1):
                input(f"Prepare for Step {step}. Press Enter when ready to start 5s countdown... ")
                print("Waiting 5 seconds...")
                time.sleep(5.0)
                if lib.haptics_get_pose(transform, position) == 0:
                    err = lib.haptics_last_error()
                    raise RuntimeError(err.decode("utf-8") if err else "failed to query pose")

                existing.append({
                    "position": [float(position[0]), float(position[1]), float(position[2])],
                    "rotation": [
                        [float(transform[0]), float(transform[4]), float(transform[8])],
                        [float(transform[1]), float(transform[5]), float(transform[9])],
                        [float(transform[2]), float(transform[6]), float(transform[10])],
                    ],
                })
                save_recording(out_path, existing)
                print(f"Recorded step {step}/{RECORD_COUNT} and saved to {out_path}")

            d, scale, o, initial_residual, residual, singular_values, perm, signs = solve_local_vector_with_axis_permutation(existing[:RECORD_COUNT])
            target_points = [target_point_for_query(i) for i in range(RECORD_COUNT)]
            predicted_points = []
            transform = build_axis_transform(perm, signs)
            for sample in [transform_pose_sample(sample, transform) for sample in existing[:RECORD_COUNT]]:
                p = np.asarray(sample["position"], dtype=float)
                r = np.asarray(sample["rotation"], dtype=float)
                predicted_points.append(scale * (r @ d + p) + o)
            print("Recording complete.")
            print(f"d = {d[0]:.9f}, {d[1]:.9f}, {d[2]:.9f}")
            print(f"scale = {scale:.9f}")
            print(f"offset = {o[0]:.9f}, {o[1]:.9f}, {o[2]:.9f}")
            print(f"axis_transform = {format_axis_transform(perm, signs)}")
            print(f"initial_residual = {initial_residual:.9f}")
            print(f"residual_norm = {residual:.9f}")
            print("singular_values = " + ", ".join(f"{value:.9f}" for value in singular_values))
            print_point_error_report(target_points, predicted_points)
            plot_point_comparison(target_points, predicted_points, title=f"Interpolated targets vs fitted device points {format_axis_transform(perm, signs)}")
            return 0
    except KeyboardInterrupt:
        return 0
    finally:
        lib.haptics_close()


if __name__ == "__main__":
    raise SystemExit(main())