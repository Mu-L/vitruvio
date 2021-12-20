﻿/* Copyright 2021 Esri
 *
 * Licensed under the Apache License Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

namespace Vitruvio
{

struct FHole
{
	TArray<FVector> Vertices;
};

struct FFace
{
	TArray<FVector> Vertices;
	TArray<FHole> Holes;
};

struct FPolygon
{
	TArray<FFace> Faces;
};

/**
 * Takes a triangulated input mesh (vertices and indices) and returns a polygon consisting of faces with holes
 * 
 * This will work for convex or concave polygons and polygons with holes.
 *
 * Note: This function is adapted from FPoly#GetOutsideWindings.
 *
 * @param InVertices	Input vertices
 * @param InIndices		Input triangle indices
 */
FPolygon GetPolygon(const TArray<FVector>& InVertices, const TArray<int32>& InIndices);

} // namespace Vitruvio
