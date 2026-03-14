#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <numeric>
#include <cassert>

// ============================================================================
// STL std::vector Exercises
// ============================================================================
//
// Instructions:
//   - Implement each function where you see "YOUR CODE HERE"
//   - Compile:  g++ -std=c++17 -o vector_exercises vector_exercises.cpp
//   - Run:      ./vector_exercises
//   - Each exercise prints PASS or FAIL
//
// ============================================================================

// ----------------------------------------------------------------------------
// Exercise 1: Build a vector
// ----------------------------------------------------------------------------
// Return a vector containing the numbers 1 through n (inclusive).
// Example: n=5 -> {1, 2, 3, 4, 5}

std::vector<int> buildRange(int n) {
    // YOUR CODE HERE
    return {};
}

// ----------------------------------------------------------------------------
// Exercise 2: Sum of elements
// ----------------------------------------------------------------------------
// Return the sum of all elements in the vector.
// Example: {3, 7, 2} -> 12

int sumElements(const std::vector<int>& v) {
    // YOUR CODE HERE
    return 0;
}

// ----------------------------------------------------------------------------
// Exercise 3: Filter evens
// ----------------------------------------------------------------------------
// Return a new vector containing only the even numbers from the input.
// Preserve the original order.
// Example: {1, 2, 3, 4, 5, 6} -> {2, 4, 6}

std::vector<int> filterEvens(const std::vector<int>& v) {
    // YOUR CODE HERE
    return {};
}

// ----------------------------------------------------------------------------
// Exercise 4: Reverse in place
// ----------------------------------------------------------------------------
// Reverse the vector in place (do NOT create a new vector).
// Example: {1, 2, 3, 4} -> {4, 3, 2, 1}

void reverseInPlace(std::vector<int>& v) {
    // YOUR CODE HERE
}

// ----------------------------------------------------------------------------
// Exercise 5: Remove duplicates
// ----------------------------------------------------------------------------
// Return a new vector with duplicates removed, keeping the FIRST occurrence
// of each value. Preserve original order.
// Example: {3, 1, 4, 1, 5, 3, 2} -> {3, 1, 4, 5, 2}

std::vector<int> removeDuplicates(const std::vector<int>& v) {
    // YOUR CODE HERE
    return {};
}

// ----------------------------------------------------------------------------
// Exercise 6: Merge sorted vectors
// ----------------------------------------------------------------------------
// Given two sorted vectors, merge them into a single sorted vector.
// Example: {1, 3, 5} and {2, 4, 6} -> {1, 2, 3, 4, 5, 6}

std::vector<int> mergeSorted(const std::vector<int>& a, const std::vector<int>& b) {
    // YOUR CODE HERE
    return {};
}

// ----------------------------------------------------------------------------
// Exercise 7: Rotate left
// ----------------------------------------------------------------------------
// Rotate the vector left by k positions.
// Example: {1, 2, 3, 4, 5} rotated by 2 -> {3, 4, 5, 1, 2}

std::vector<int> rotateLeft(const std::vector<int>& v, int k) {
    // YOUR CODE HERE
    return {};
}

// ----------------------------------------------------------------------------
// Exercise 8: Flatten 2D vector
// ----------------------------------------------------------------------------
// Given a 2D vector (vector of vectors), flatten it into a single 1D vector.
// Example: {{1, 2}, {3}, {4, 5, 6}} -> {1, 2, 3, 4, 5, 6}

std::vector<int> flatten(const std::vector<std::vector<int>>& v) {
    // YOUR CODE HERE
    return {};
}

// ----------------------------------------------------------------------------
// Exercise 9: Sliding window max
// ----------------------------------------------------------------------------
// Return a vector of the maximum value in each sliding window of size k.
// Example: {1, 3, 2, 5, 4} with k=3 -> {3, 5, 5}
//   window [1,3,2]->3, [3,2,5]->5, [2,5,4]->5

std::vector<int> slidingWindowMax(const std::vector<int>& v, int k) {
    // YOUR CODE HERE
    return {};
}

// ----------------------------------------------------------------------------
// Exercise 10: Matrix transpose
// ----------------------------------------------------------------------------
// Transpose a 2D vector (matrix). Rows become columns and vice versa.
// Example: {{1, 2, 3},    ->   {{1, 4},
//           {4, 5, 6}}          {2, 5},
//                               {3, 6}}

std::vector<std::vector<int>> transpose(const std::vector<std::vector<int>>& matrix) {
    // YOUR CODE HERE
    return {};
}

// ============================================================================
// Test harness - do not modify below this line
// ============================================================================

void test(const std::string& name, bool passed) {
    std::cout << (passed ? "  PASS" : "  FAIL") << "  " << name << "\n";
}

int main() {
    std::cout << "=== std::vector Exercises ===\n\n";

    // Ex 1
    test("Ex1: buildRange(5)",
         buildRange(5) == std::vector<int>{1, 2, 3, 4, 5});
    test("Ex1: buildRange(0)",
         buildRange(0) == std::vector<int>{});

    // Ex 2
    test("Ex2: sumElements({3,7,2})",
         sumElements({3, 7, 2}) == 12);
    test("Ex2: sumElements(empty)",
         sumElements({}) == 0);

    // Ex 3
    test("Ex3: filterEvens({1,2,3,4,5,6})",
         filterEvens({1, 2, 3, 4, 5, 6}) == std::vector<int>{2, 4, 6});
    test("Ex3: filterEvens({1,3,5})",
         filterEvens({1, 3, 5}) == std::vector<int>{});

    // Ex 4
    {
        std::vector<int> v = {1, 2, 3, 4};
        reverseInPlace(v);
        test("Ex4: reverseInPlace({1,2,3,4})",
             v == std::vector<int>{4, 3, 2, 1});
    }

    // Ex 5
    test("Ex5: removeDuplicates({3,1,4,1,5,3,2})",
         removeDuplicates({3, 1, 4, 1, 5, 3, 2}) == std::vector<int>{3, 1, 4, 5, 2});

    // Ex 6
    test("Ex6: mergeSorted({1,3,5},{2,4,6})",
         mergeSorted({1, 3, 5}, {2, 4, 6}) == std::vector<int>{1, 2, 3, 4, 5, 6});
    test("Ex6: mergeSorted({1,2},{3})",
         mergeSorted({1, 2}, {3}) == std::vector<int>{1, 2, 3});

    // Ex 7
    test("Ex7: rotateLeft({1,2,3,4,5}, 2)",
         rotateLeft({1, 2, 3, 4, 5}, 2) == std::vector<int>{3, 4, 5, 1, 2});
    test("Ex7: rotateLeft({1,2,3}, 0)",
         rotateLeft({1, 2, 3}, 0) == std::vector<int>{1, 2, 3});

    // Ex 8
    test("Ex8: flatten({{1,2},{3},{4,5,6}})",
         flatten({{1, 2}, {3}, {4, 5, 6}}) == std::vector<int>{1, 2, 3, 4, 5, 6});

    // Ex 9
    test("Ex9: slidingWindowMax({1,3,2,5,4}, 3)",
         slidingWindowMax({1, 3, 2, 5, 4}, 3) == std::vector<int>{3, 5, 5});
    test("Ex9: slidingWindowMax({4,3,2,1}, 2)",
         slidingWindowMax({4, 3, 2, 1}, 2) == std::vector<int>{4, 3, 2});

    // Ex 10
    {
        std::vector<std::vector<int>> input = {{1, 2, 3}, {4, 5, 6}};
        std::vector<std::vector<int>> expected = {{1, 4}, {2, 5}, {3, 6}};
        test("Ex10: transpose(2x3 matrix)", transpose(input) == expected);
    }

    std::cout << "\nDone! Fix any FAILs and re-run.\n";
    return 0;
}
