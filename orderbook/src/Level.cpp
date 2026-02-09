#include <iostream>
#include "LOB/Level.h"
#include "LOB/Macros.h"

void Level::push_back(Order* order) {
    if (LOB_UNLIKELY(!order)) return;
    
    if (order_number == 0) {
        // First order in the level
        head = tail = order;
        order->set_prev_order(nullptr);
        order->set_next_order(nullptr);
    } else {
        // Append to tail
        tail->set_next_order(order);
        order->set_prev_order(tail);
        order->set_next_order(nullptr);
        tail = order;
    }
    
    total_volume += order->get_remaining_volume();
    order_number++;
}

Order* Level::pop_front() {
    if (order_number == 0) {
        return nullptr;
    }
    
    Order* old_head = head;
    
    if (order_number == 1) {
        // Last order
        head = tail = nullptr;
    } else {
        // Move head to next
        head = head->get_next_order();
        head->set_prev_order(nullptr);
        old_head->set_next_order(nullptr);
    }
    
    total_volume -= old_head->get_remaining_volume();
    order_number--;
    
    return old_head;
}

void Level::erase(Order* order) {
    if (LOB_UNLIKELY(!order || order_number == 0)) return;
    
    if (order_number == 1) {
        // Only one order
        head = tail = nullptr;
    } else if (order == head) {
        // Removing head
        head = order->get_next_order();
        if (head) {
            head->set_prev_order(nullptr);
        }
    } else if (order == tail) {
        // Removing tail
        tail = order->get_prev_order();
        if (tail) {
            tail->set_next_order(nullptr);
        }
    } else {
        // Removing from middle
        Order* prev = order->get_prev_order();
        Order* next = order->get_next_order();
        if (prev) prev->set_next_order(next);
        if (next) next->set_prev_order(prev);
    }
    
    // Clear order's links
    order->set_prev_order(nullptr);
    order->set_next_order(nullptr);
    
    total_volume -= order->get_remaining_volume();
    order_number--;
}

void Level::print() const {
    std::cout << "Level Price: " << limit_price << std::endl;
    std::cout << "Number of Orders: " << order_number << std::endl;
    std::cout << "Total Volume: " << total_volume << std::endl;

    Order* current = head;
    while (current) {
        std::cout << "\t";
        current->print();
        current = current->get_next_order();
    }
}
