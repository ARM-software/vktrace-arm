/**
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2019-2021 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#pragma once

#include <semaphore.h>
#include <pthread.h>
#include "vulkan/vulkan_core.h"
#include "vulkan/vulkan.h"
#include "vulkan/vk_icd.h"

/* Forward declare the page flip thread function so we can befriend it. */
void *page_flip_thread(void *ptr);

namespace offscreen
{

/**
 * brief semaphore with a safe relative timed wait
 *
 * sem_timedwait takes an absolute time, based on CLOCK_REALTIME. Simply
 * taking the current time and adding on a relative timeout is not correct,
 * as the system time may change, resulting in an incorrect timeout period
 * (potentially by a significant amount).
 *
 * We therefore have to re-engineer semaphores using condition variables.
 *
 * This code does not use the C++ standard library to avoid exceptions.
 */
class timed_sem
{
public:
    /* copying not implemented */
    timed_sem &operator=(const timed_sem &) = delete;
    timed_sem(const timed_sem &) = delete;

    timed_sem();
    ~timed_sem();

    /**
     * @brief initializes the semaphore
     *
     * @param count initial value of the semaphore
     * @retval VK_ERROR_OUT_OF_HOST_MEMORY out of memory condition from pthread calls
     * @retval VK_SUCCESS on success
     */
    VkResult init(unsigned count);

    /**
     * @brief decrement semaphore, waiting (with timeout) if the value is 0
     *
     * @param timeout time to wait (ns). 0 doesn't block, UINT64_MAX waits indefinately.
     * @retval VK_TIMEOUT timeout was non-zero and reached the timeout
     * @retval VK_NOT_READY timeout was zero and count is 0
     * @retval VK_SUCCESS on success
     */
    VkResult wait(uint64_t timeout);

    /**
     * @brief increment semaphore, potentially unblocking a waiting thread
     */
    void post();

    private:
    /**
     * @brief true if the semaphore has been initialized
     *
     * Determines if the destructor should cleanup the mutex and cond.
     */
    bool initialized;
    /**
     * @brief semaphore value
     */
    unsigned m_count;

    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};

/**
 * @brief Base swapchain class
 *
 * - the swapchain implementations inherit from this class.
 * - the vkswapchain will hold a pointer to this class.
 * - much of the swapchain implementation is done by this class, as the only things needed
 *   in the implementation are how to create a presentable image and how to present an image.
 */
class swapchain
{
public:
    swapchain(const VkAllocationCallbacks *pAllocator);

    virtual ~swapchain(){};
    /**
     * @brief Create swapchain.
     *
     * Perform all swapchain initialization, create presentable images etc.
     */
    VkResult init(VkDevice device, const VkSwapchainCreateInfoKHR *pSwapchainCreateInfo);

    /**
     * @brief Acquires a free image.
     *
     * Current implementation blocks until a free image is available.
     *
     * @param timeout Unused since we block until a free image is available.
     *
     * @param semaphore A semaphore signaled once an image is acquired.
     *
     * @param fence A fence signaled once an image is acquired.
     *
     * @param pImageIndex The index of the acquired image.
     *
     * @return VK_SUCCESS on completion.
     */
    VkResult acquire_next_image(uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex);

    /**
     * @brief Gets the number of swapchain images or a number of at most
     * m_num_swapchain_images images.
     *
     * @param pSwapchainImageCount Used to return number of images in
     * the swapchain if second parameter is nullptr or represents the
     * number of images to be returned in second parameter.
     *
     * @param pSwapchainImage Array of VkImage handles.
     *
     * @return If number of requested images is less than the number of available
     * images in the swapchain returns VK_INCOMPLETE otherwise VK_SUCCESS.
     */
    VkResult get_swapchain_images(uint32_t *pSwapchainImageCount, VkImage *pSwapchainImage);

    /**
     * @brief Submits a present request for the supplied image.
     *
     * @param queue The queue to which the submission will be made to.
     *
     * @param pPresentInfo Information about the swapchain and image to be presented.
     *
     * @param imageIndex The index of the image to be presented.
     *
     * @return If queue submission fails returns error of vkQueueSubmit, if the
     * swapchain has a descendant who started presenting returns VK_ERROR_OUT_OF_DATE_KHR,
     * otherwise returns VK_SUCCESS.
     */
    VkResult queue_present(VkQueue queue, const VkPresentInfoKHR *pPresentInfo, const uint32_t imageIndex);

    /**
     * @brief Returns the creation info used during construction of this swapchain.
     */
    VkSwapchainCreateInfoKHR get_create_info() const
    {
        return m_create_info;
    }

    /**
     * @brief Validates image creation parameters against a swapchain's image creation parameters.
     *
     * Sometimes it is useful for an image to be created that is compatible with the images internal to
     * the swapchain, for example VK_KHR_device_group. This method validates the creation parameters
     * of the image against the creation parameters of the swapchain's internal images.
     *
     * @param img_create_info Creation parameters that will be used to construct the image.
     *
     * @return True if the creation parameters are compatible, otherwise false.
     */
    bool validate_implied_image_creation_parameters(const VkImageCreateInfo &img_create_info) const;

    /**
     * @brief Creates an image that will be bound to the memory for one of the images of this swapcahin.
     *
     * Images created with VkImageSwapchainCreateInfoKHR extension structure containing a valid swapchain
     * handle are to be bound to the underlying memory of one of the swapchain's images. This method creates
     * the VkImage. The returned image *must* be bound prior to use.
     *
     * The arguments are identical as those for vkCreateImage.
     */
    virtual VkResult create_swapchain_image(VkDevice device, const VkImageCreateInfo *pCreateInfo,
                                            const VkAllocationCallbacks *pAllocator, VkImage *pImage) = 0;

    /**
     * @brief Binds a provided VkImage to the memory of a swapchain image.
     *
     * Images created as bound to a swapchain must call vkBindImageMemory with a VkBindImageMemorySwapchainInfoKHR
     * extension structure specified, which contains the swapchain and swapchain image index to bind the new image
     * to.
     *
     * This method performs the actual binding operation with the (WSI specific) underlying memory.
     */
    virtual VkResult bind_swapchain_image(VkDevice device, const VkAllocationCallbacks *pAllocator, VkImage image,
                                            uint32_t index) = 0;

protected:
    friend void * ::page_flip_thread(void *ptr);
    struct swapchain_image
    {
        /* Implementation specific data */
        void *image_data;
        enum image_status
        {
            INVALID,
            ACQUIRED,
            PENDING,
            PRESENTED,
            FREE
        };
        VkImage image;
        image_status status;
        VkFence present_fence;
    };

    /**
     * @brief Handle to the page flip thread.
     */
    pthread_t m_page_flip_thread;

    /**
     * @brief In case we encounter threading or drm errors we need a way to
     * notify the user of the failure. When this flag is false, acquire_next_image
     * will return an error code.
     */
    bool m_is_valid;


    struct ring_buffer
    {
        /* Ring buffer to hold the image indexes. */
        uint32_t *ring;
        /* Head of the ring. */
        uint32_t head;
        /* End of the ring. */
        uint32_t tail;
        /* Size of the ring. */
        uint32_t size;
    };
    /**
     * @brief A semaphorenamespace vulkan
     { to be signalled once a page flip even occurs.
     */
    sem_t m_page_flip_semaphore;

    /**
     * @brief A semaphore to be signalled once the swapchain has one frame on screen.
     */
    sem_t m_start_present_semaphore;

    /**
     * @brief Defines if the pthread_t and sem_t members of the class are defined.
     *
     * As they are opaque types theer's no known invalid value that we ca initialize to,
     * and therefore determine if we need to cleanup.
     */
    bool m_thread_sem_defined;

    /**
     * @brief A flag to track if it is the first present for the chain.
     */
    bool m_first_present;

    /**
     * @brief In order to present the images in a FIFO order we implement
     * a ring buffer to hold the images queued for presentation. Since the
     * two pointers (head and tail) are used by different
     * therads and we do not allow the application to acquire more images
     * than we have we eliminate race conditions.
     */
    ring_buffer m_pending_buffer_pool;

    /**
     * @brief The number of swapchain images.
     */
    uint32_t m_num_swapchain_images;

    /**
     * @brief Array of images in the swapchain.
     */
    swapchain_image *m_swapchain_images;

    /**
     * @brief User provided memory allocation callbacks.
     */
    const VkAllocationCallbacks *m_alloc_callbacks;

    /**
     * @brief Handle to the surface object this swapchain will present images to.
     */
    VkSurfaceKHR m_surface;

    /**
     * @brief present mode to use for this swapchain
     */
    VkPresentModeKHR m_present_mode;

    /**
     * @brief Descendant of this swapchain.
     * Used to check whether or not a descendant of this swapchain has started
     * presenting images to the surface already. If it has, any calls to queuePresent
     * for this swapchain will return VK_ERROR_OUT_OF_DATE_KHR.
     */
    VkSwapchainKHR m_descendant;

    /**
     * @brief Ancestor of this swapchain.
     * Used to check whether the ancestor swapchain has completed all of its
     * pending page flips (this is required before this swapchain presents for the
     * first time.
     */
    VkSwapchainKHR m_ancestor;

    /**
     * @brief Handle to the logical device the swapchain is created for.
     */
    VkDevice m_device;

    /**
     * @brief Handle to the queue used for signalling submissions
     */
    VkQueue m_queue;

    /**
     * @brief Copy of the creation parameters for this swapchain.
     * These may be needed later to verify correctness of other components, for instance
     * VkImageSwapchainCreateInfoKHR images.
     */
    VkSwapchainCreateInfoKHR m_create_info;

    /*
     * @brief Method to wait on all pending buffers to be displayed.
     */
    void wait_for_pending_buffers();

    /**
     * @brief Remove cached ancestor.
     */
    void clear_ancestor();

    /**
     * @brief Remove cached descendant.
     */
    void clear_descendant();

    /**
     * @brief Deprecate this swapchain.
     *
     * If an application replaces an old swapchain with a new one, the older swapchain
     * needs to be deprecated. This method releases all the FREE images and sets the
     * descendant of the swapchain. We do not need to care about images in other states
     * at this point since they will be released by the page flip thread.
     *
     * @param descendant Handle to the descendant swapchain.
     */
    void deprecate(VkSwapchainKHR descendant);

    /**
     * @brief Platform specific initialization
     */
    virtual VkResult init_platform(VkDevice device, const VkSwapchainCreateInfoKHR *pSwapchainCreateInfo) = 0;

    /**
     * @brief Base swapchain teardown.
     *
     * Even though the inheritance gives us a nice way to defer display specific allocation
     * and presentation outside of the base class, it however robs the children classes - which
     * also happen to do some of their state setting - the oppurtunity to do the last clean up
     * call, as the base class' destructor is called at the end. This method provides a way to do it.
     * The destructor is a virtual function and much of the swapchain teardown happens in this method
     * which gets called from the child's destructor.
     */
    void teardown();

    /**
     * @brief Creates a new swapchain image.
     *
     * @param image_create_info Data to be used to create the image.
     *
     * @param image Handle to the image.
     *
     * @return If image creation is successful returns VK_SUCCESS, otherwise
     * will return VK_ERROR_OUT_OF_DEVICE_MEMORY or VK_ERROR_INITIALIZATION_FAILED
     * depending on the error that occured.
     */
    virtual VkResult create_present_image(const VkImageCreateInfo &image_create_info, swapchain_image &image) = 0;

    /**
     * @brief Performs end of frame instrumentation API calls.
     *
     * Implementing classes should call @ref mali_vulkan_instrument_frame_end with the image data for their swapchain.
     *
     * @param image The swapchain image for this frame.
     * @param queue The queue being used for the presentation of this frame.
     *
     * @return @see mali_vulkan_instrument_frame_end
     */
    virtual VkResult instrument_frame_end(swapchain_image &image, VkQueue queue) = 0;

    /**
     * @brief Method to present and image
     *
     * @param pendingIndex Index of the pending image to be presented.
     *
     */
    virtual void present_image(uint32_t pendingIndex) = 0;

    /**
     * @brief Transition a presented image to free.
     *
     * Called by swapchain implementation when a new image has been presented.
     *
     * @param presentedIndex Index of the image to be marked as free.
     */
    void unpresent_image(uint32_t presentedIndex);

    /**
     * @brief Method to release a swapchain image
     *
     * @param image Handle to the image about to be released.
     */
    virtual void release_present_image(swapchain_image &image){};

    /**
     * @brief Hook for any actions to free up a buffer for acquire
     *
     * @param[in,out] timeout time to wait, in nanoseconds. 0 doesn't block,
     *                        UINT64_MAX waits indefinately. The timeout should
     *                        be updated if a sleep is required - this can
     *                        be set to 0 if the semaphore is now not expected
     *                        block.
     */
    virtual VkResult get_free_buffer(uint64_t *timeout)
    {
        return VK_SUCCESS;
    }

private:
    /**
     * @brief Wait for a buffer to become free.
     */
    VkResult wait_for_free_buffer(uint64_t timeout);

    /**
     * @brief A semaphore to be signalled once a free image becomes available.
     *
     * Uses a custom semaphore implementation that uses a condition variable.
     * it is slower, but has a safe timedwait implementation.
     *
     * This is kept private as waiting should be done via wait_for_free_buffer().
     */
    timed_sem m_free_image_semaphore;
};

/**
 * @brief Headless swapchain class.
 *
 * This class is mostly empty, because all the swapchain stuff is handled by the swapchain class,
 * which we inherit. This class only provides a way to create an image and page-flip ops.
 */
class swapchain_headless : public swapchain
{
public:
    explicit swapchain_headless(const VkAllocationCallbacks *pAllocator);
    ~swapchain_headless();

    VkResult create_swapchain_image(VkDevice device, const VkImageCreateInfo *pCreateInfo,
                                    const VkAllocationCallbacks *pAllocator, VkImage *pImage) override;
    VkResult bind_swapchain_image(VkDevice device, const VkAllocationCallbacks *pAllocator, VkImage image,
                                    uint32_t index) override;

protected:
    /**
     * @brief Platform specific init
     */
    VkResult init_platform(VkDevice device, const VkSwapchainCreateInfoKHR *pSwapchainCreateInfo) override
    {
        return VK_SUCCESS;
    };

    /**
     * @brief Creates a new swapchain image.
     *
     * @param image_create_info Data to be used to create the image.
     *
     * @param image Handle to the image.
     *
     * @return If image creation is successful returns VK_SUCCESS, otherwise
     * will return VK_ERROR_OUT_OF_DEVICE_MEMORY or VK_ERROR_INITIALIZATION_FAILED
     * depending on the error that occured.
     */
    VkResult create_present_image(const VkImageCreateInfo &image_create_info, swapchain_image &image) override;

    /**
     * @brief Performs end of frame instrumentation API calls.
     *
     * Calls @ref mali_vulkan_instrument_frame_end with the image data for this swapchain.
     *
     * @param image The swapchain image for this frame.
     * @param queue The queue being used for the presentation of this frame.
     *
     * @return @see mali_vulkan_instrument_frame_end
     */
    VkResult instrument_frame_end(swapchain_image &image, VkQueue queue) override;

    /**
     * @brief Method to perform a present - just calls unpresent_image on headless
     *
     * @param pendingIndex Index of the pending image to be presented.
     *
     */
    void present_image(uint32_t pendingIndex) override;

    /**
     * @brief Method to release a swapchain image
     *
     * @param image Handle to the image about to be released.
     */
    void release_present_image(swapchain_image &image) override;
};

}