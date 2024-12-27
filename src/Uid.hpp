#include <limits>
#include <vector>

template <typename T> class Uid {
    static_assert(std::numeric_limits<T>::min() < std::numeric_limits<T>::max(),
                  "Type needs numeric limits");

  public:
    Uid() : counter(std::numeric_limits<T>::min()) {}
    Uid(T start) { counter = start; }

    T get() {
        if (!free_list.empty()) {
            auto tmp = free_list.back();
            free_list.pop_back();
            return tmp;
        }
        else {
            return counter++;
        }
    }
    void free(T id) {
        free_list.push_back(id);
    }

  private:
    T counter;
    std::vector<T> free_list;
};
