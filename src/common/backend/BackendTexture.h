#pragma once

#include <memory>

class Texture {
  public:
	Texture() {};

	template <typename T>
	Texture(T* ptr_, int32_t width_, int32_t height_, void (*deleter_)(T*))
		: ptr(ptr_, deleter_), width(width_), height(height_) {}

	explicit operator bool() const {
		return ptr.get() != nullptr;
	}

	bool operator==(const Texture& other) const {
		return ptr == other.ptr;
	}

	bool operator!=(const Texture& other) const {
		return !(*this == other);
	}

	bool operator<(const Texture& other) const {
		return ptr < other.ptr;
	}

	template <typename T>
	T* as() {
		return reinterpret_cast<T*>(ptr.get());
	}

	template <typename T>
	const T* as() const {
		return reinterpret_cast<const T*>(ptr.get());
	}

	std::shared_ptr<void> ptr;
	int32_t width = 0;
	int32_t height = 0;
};