/**
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2019-2021 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/**
 * @file mali_vulkan_headless_swapchain.cpp
 *
 * @brief Contains the implementation for a headless swapchain.
 */

#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include "offscreen_render.h"
#include "headless_surface.h"
#include "headless_swapchain.h"

#define CSTD_LIKELY(x) (x)
#define vkresult_is_success(result) CSTD_LIKELY(VkResult::VK_SUCCESS == (result))
#define WSI_PRINT_ERROR(...) fprintf(stderr, ##__VA_ARGS__)

/**
 * @brief Per swapchain thread function that handles page flipping.
 * This thread should be running for the lifetime of the swapchain.
 * The thread simply calls the implementation's present_image() method.
 * There are 3 main cases we cover here:
 *
 * 1. On the first present of the swapchain if the swapchain has
 *    an ancestor we must wait for it to finish presenting.
 * 2. The normal use case where we do page flipping, in this
 *    case change the currently PRESENTED image with the oldest
 *    PENDING image.
 * 3. If the enqueued image is marked as FREE it means the
 *    descendant of the swapchain has started presenting so we
 *    should release the image and continue.
 *
 * The function always waits on the page_flip_semaphore of the
 * swapchain. Once it passes that we must wait for the fence of the
 * oldest pending image to be signalled, this means that the gpu has
 * finished rendering to it and we can present it. From there on the
 * logic splits into the above 3 cases and if an image has been
 * presented then the old one is marked as FREE and the free_image
 * semaphore of the swapchain will be posted.
 **/
__attribute__((noreturn)) void *page_flip_thread(void *ptr)
{
    offscreen::swapchain *sc = (offscreen::swapchain *)ptr;
    offscreen::swapchain::swapchain_image *sc_images = sc->m_swapchain_images;
    VkResult vk_res = VK_SUCCESS;

    int res = 0;
    CSTD_UNUSED(res); /* Avoid compiler warnings if assertions are disabled. */

    uint64_t timeout = UINT64_MAX;

#if MALI_CINSTR_PLUG_DUMP
    /* Magic value (UINT64_MAX - 42) excludes it from a deadlock check,
     * when we have a active plug in place.
     */
    timeout = UINT64_MAX - 42;
#endif

    while (1)
    {
        /* Waiting for the page_flip_semaphore which will be signalled once there is an
        * image to display.*/
        res = sem_wait(&sc->m_page_flip_semaphore);
        assert(res == 0);

        /* We want to present the oldest queued for present image from our present queue,
        * which we can find at the sc->pending_buffer_pool.head index. */
        uint32_t pendingIndex = sc->m_pending_buffer_pool.ring[sc->m_pending_buffer_pool.head];
        sc->m_pending_buffer_pool.head = (sc->m_pending_buffer_pool.head + 1) % sc->m_pending_buffer_pool.size;

        /* We wait for the fence of the oldest pending image to be signalled. */
        vk_res = vkWaitForFences(sc->m_device, 1, &sc_images[pendingIndex].present_fence, VK_TRUE, timeout);
        if (vk_res != VK_SUCCESS)
        {
            sc->m_is_valid = false;
            sc->m_free_image_semaphore.post();
            continue;
        }

        /* If the descendant has started presenting the queue_present operation has marked the image
        * as FREE so we simply release it and continue. */
        if (sc_images[pendingIndex].status == offscreen::swapchain::swapchain_image::FREE)
        {
            sc->release_present_image(sc_images[pendingIndex]);
            sc->m_free_image_semaphore.post();
            continue;
        }

        /* First present of the swapchain. If it has an ancestor, wait until all the pending buffers
        * from the ancestor have finished page flipping before we set mode. */
        if (sc->m_first_present)
        {
            if (sc->m_ancestor != VK_NULL_HANDLE)
            {
                offscreen::swapchain *ancestor = reinterpret_cast<offscreen::swapchain *>(sc->m_ancestor);
                ancestor->wait_for_pending_buffers();
            }
            sem_post(&sc->m_start_present_semaphore);
            sc->present_image(pendingIndex);
            sc->m_first_present = false;
        }
        /* The swapchain has already started presenting. */
        else
        {
            sc->present_image(pendingIndex);
        }
    }
}

namespace offscreen
{

timed_sem::timed_sem()
    : initialized(false)
    , m_count(0)
{
}

timed_sem::~timed_sem()
{
    int res;
    (void)res; /* unused when NDEBUG */

    if (initialized)
    {
        res = pthread_cond_destroy(&m_cond);
        assert(res == 0); /* only programming error (EBUSY, EINVAL) */

        res = pthread_mutex_destroy(&m_mutex);
        assert(res == 0); /* only programming error (EBUSY, EINVAL) */
    }
}

VkResult timed_sem::init(unsigned count)
{
    int res;
    m_count = count;
    pthread_condattr_t attr;
    res = pthread_condattr_init(&attr);
    /* the only failure that can occur is ENOMEM */
    assert(res == 0 || res == ENOMEM);
    if (res != 0)
    {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    res = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    /* only programming error can cause _setclock to fail */
    assert(res == 0);
    res = pthread_cond_init(&m_cond, &attr);
    /* the only failure that can occur that is not programming error is ENOMEM */
    assert(res == 0 || res == ENOMEM);
    if (res != 0)
    {
        pthread_condattr_destroy(&attr);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    res = pthread_condattr_destroy(&attr);
    /* only programming error can cause _destroy to fail */
    assert(res == 0);
    res = pthread_mutex_init(&m_mutex, NULL);
    /* only programming errors can result in failure */
    assert(res == 0);
    initialized = true;

    return VK_SUCCESS;
}

VkResult timed_sem::wait(uint64_t timeout)
{
    VkResult retval = VK_SUCCESS;
    int res;
    assert(initialized);
    res = pthread_mutex_lock(&m_mutex);
    assert(res == 0); /* only fails with programming error (EINVAL) */

    if (m_count == 0)
    {
        switch (timeout)
        {
        case 0:
            retval = VK_NOT_READY;
            break;
        case UINT64_MAX:
            res = pthread_cond_wait(&m_cond, &m_mutex);
            assert(res == 0); /* only fails with programming error (EINVAL) */
            break;
        default:
            struct timespec diff = { /* narrowing casts */
                                    static_cast<time_t>(timeout / (1000 * 1000 * 1000)),
                                    static_cast<long>(timeout % (1000 * 1000 * 1000))
            };

            struct timespec now;
            res = clock_gettime(CLOCK_MONOTONIC, &now);
            assert(res == 0); /* only fails with programming error (EINVAL, EFAULT, EPERM) */

            /* add diff to now, handling overflow */
            struct timespec end = { now.tv_sec + diff.tv_sec, now.tv_nsec + diff.tv_nsec };

            if (end.tv_nsec >= 1000 * 1000 * 1000)
            {
                end.tv_nsec -= 1000 * 1000 * 1000;
                end.tv_sec++;
            }

            res = pthread_cond_timedwait(&m_cond, &m_mutex, &end);
            /* only fails with programming error, other than timeout */
            assert(res == 0 || res == ETIMEDOUT);
            if (res != 0)
            {
                retval = VK_TIMEOUT;
            }
        }
    }
    if (vkresult_is_success(retval))
    {
        assert(m_count > 0);
        m_count--;
    }
    res = pthread_mutex_unlock(&m_mutex);
    assert(res == 0); /* only fails with programming error (EPERM) */

    return retval;
}

void timed_sem::post()
{
    int res;
    (void)res; /* unused when NDEBUG */

    assert(initialized);

    res = pthread_mutex_lock(&m_mutex);
    assert(res == 0); /* only fails with programming error (EINVAL) */
    m_count++;
    res = pthread_cond_signal(&m_cond);
    assert(res == 0); /* only fails with programming error (EINVAL) */
    res = pthread_mutex_unlock(&m_mutex);
    assert(res == 0); /* only fails with programming error (EPERM) */
}

void swapchain::unpresent_image(uint32_t presentedIndex)
{
    m_swapchain_images[presentedIndex].status = offscreen::swapchain::swapchain_image::FREE;
    if (m_descendant != VK_NULL_HANDLE)
    {
        release_present_image(m_swapchain_images[presentedIndex]);
    }
    m_free_image_semaphore.post();
}


swapchain::swapchain(const VkAllocationCallbacks *pAllocator)
    : m_page_flip_thread()
    , m_is_valid(false)
    , m_thread_sem_defined(false)
    , m_first_present(true)
    , m_pending_buffer_pool{ nullptr, 0, 0, 0 }
    , m_num_swapchain_images(0)
    , m_swapchain_images(nullptr)
    , m_alloc_callbacks(pAllocator)
    , m_surface(VK_NULL_HANDLE)
    , m_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
    , m_descendant(VK_NULL_HANDLE)
    , m_ancestor(VK_NULL_HANDLE)
    , m_device(VK_NULL_HANDLE)
    , m_queue(VK_NULL_HANDLE)
{
}

VkResult swapchain::init(VkDevice device, const VkSwapchainCreateInfoKHR *pSwapchainCreateInfo)
{
    assert(device != VK_NULL_HANDLE);
    assert(pSwapchainCreateInfo != nullptr);
    assert(pSwapchainCreateInfo->surface != VK_NULL_HANDLE);

    int res;
    VkResult result;

    m_device = device;
    m_surface = pSwapchainCreateInfo->surface;

    /* Check presentMode has a compatible value with swapchain - everything else should be taken care at image creation.*/
    static const VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_FIFO_RELAXED_KHR };
    bool present_mode_found = false;
    for (uint32_t i = 0; i < NELEMS(present_modes) && !present_mode_found; i++)
    {
        if (pSwapchainCreateInfo->presentMode == present_modes[i])
        {
            present_mode_found = true;
        }
    }

    if (!present_mode_found)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    m_num_swapchain_images = pSwapchainCreateInfo->minImageCount;

    size_t images_alloc_size = sizeof(swapchain_image) * m_num_swapchain_images;
    if (m_alloc_callbacks != nullptr)
    {
        m_swapchain_images = static_cast<swapchain_image *>(
            m_alloc_callbacks->pfnAllocation(m_alloc_callbacks->pUserData, images_alloc_size, alignof(swapchain_image),
                                             VK_SYSTEM_ALLOCATION_SCOPE_OBJECT));
    }
    else
    {
        m_swapchain_images = static_cast<swapchain_image *>(malloc(images_alloc_size));
    }
    if (m_swapchain_images == nullptr)
    {
        m_num_swapchain_images = 0;
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    /* We have allocated images, we can call the platform init function if something needs to be done. */
    result = init_platform(device, pSwapchainCreateInfo);
    if (result != VK_SUCCESS)
    {
        return result;
    }

    for (uint32_t i = 0; i < m_num_swapchain_images; ++i)
    {
        /* Init image to invalid values. */
        m_swapchain_images[i].image = VK_NULL_HANDLE;
        m_swapchain_images[i].present_fence = VK_NULL_HANDLE;
        m_swapchain_images[i].status = swapchain_image::INVALID;
        m_swapchain_images[i].image_data = nullptr;
    }
    /* Initialize ring buffer. */
    if (m_alloc_callbacks != nullptr)
    {
        m_pending_buffer_pool.ring = static_cast<uint32_t *>(
            m_alloc_callbacks->pfnAllocation(m_alloc_callbacks->pUserData, sizeof(uint32_t) * m_num_swapchain_images,
                                             alignof(uint32_t), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT));
    }
    else
    {
        m_pending_buffer_pool.ring = static_cast<uint32_t *>(malloc(sizeof(uint32_t) * m_num_swapchain_images));
    }

    if (m_pending_buffer_pool.ring == nullptr)
    {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    m_pending_buffer_pool.head = 0;
    m_pending_buffer_pool.tail = 0;
    m_pending_buffer_pool.size = m_num_swapchain_images;
    VkImageCreateInfo image_create_info = {};
    image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_create_info.pNext = nullptr;
    image_create_info.imageType = VK_IMAGE_TYPE_2D;
    image_create_info.format = pSwapchainCreateInfo->imageFormat;
    image_create_info.extent = { pSwapchainCreateInfo->imageExtent.width, pSwapchainCreateInfo->imageExtent.height, 1 };
    image_create_info.mipLevels = 1;
    image_create_info.arrayLayers = pSwapchainCreateInfo->imageArrayLayers;
    image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_create_info.usage = pSwapchainCreateInfo->imageUsage;
    image_create_info.flags = 0;
    image_create_info.sharingMode = pSwapchainCreateInfo->imageSharingMode;
    image_create_info.queueFamilyIndexCount = pSwapchainCreateInfo->queueFamilyIndexCount;
    image_create_info.pQueueFamilyIndices = pSwapchainCreateInfo->pQueueFamilyIndices;
    image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    result = m_free_image_semaphore.init(m_num_swapchain_images);
    if (result != VK_SUCCESS)
    {
        assert(result == VK_ERROR_OUT_OF_HOST_MEMORY);
        return result;
    }

    for (unsigned i = 0; i < m_num_swapchain_images; i++)
    {
        result = create_present_image(image_create_info, m_swapchain_images[i]);
        if (result != VK_SUCCESS)
        {
            return result;
        }
    }

    vkGetDeviceQueue(m_device, 0, 0, &m_queue);

    /* Setup semaphore for signalling pageflip thread. */
    res = sem_init(&m_page_flip_semaphore, 0, 0);

    /* Only programming error can cause this to fail. */
    assert(res == 0);
    if (res != 0)
    {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    res = sem_init(&m_start_present_semaphore, 0, 0);
    /* Only programming error can cause this to fail. */
    assert(res == 0);
    if (res != 0)
    {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    m_thread_sem_defined = true;

    /* Launch page flipping thread */
    res = pthread_create(&m_page_flip_thread, NULL, page_flip_thread, (void *)this);
    if (res < 0)
    {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    /* Release the swapchain images of the old swapchain in order
     * to free up memory for new swapchain. This is necessary especially
     * on platform with limited display memory size.
     *
     * NB: This must be done last in initialization, when the rest of
     * the swapchain is valid.
     */
    if (pSwapchainCreateInfo->oldSwapchain != VK_NULL_HANDLE)
    {
        /* Set ancestor. */
        m_ancestor = pSwapchainCreateInfo->oldSwapchain;

        swapchain *ancestor = reinterpret_cast<swapchain *>(m_ancestor);
        ancestor->deprecate(reinterpret_cast<VkSwapchainKHR>(this));
    }

    m_is_valid = true;

    /* Store copy of creation info for later use. */
    m_create_info = *pSwapchainCreateInfo;

    return VK_SUCCESS;
}

static inline int pthread_cancel(pthread_t thread) {
    return pthread_kill(thread, SIGUSR1);
}

void swapchain::teardown()
{
    /* This method will block until all resources associated with this swapchain
     * are released. Images in the ACQUIRED or FREE state can be freed
     * immediately. For images in the PRESENTED state, we will block until the
     * presentation engine is finished with them.
     */
    int res;
    bool descendantStartedPresenting = false;

    if (m_descendant != VK_NULL_HANDLE)
    {
        swapchain *desc = reinterpret_cast<swapchain *>(m_descendant);
        for (uint32_t i = 0; i < desc->m_num_swapchain_images; ++i)
        {
            if (desc->m_swapchain_images[i].status == swapchain_image::PRESENTED ||
                desc->m_swapchain_images[i].status == swapchain_image::PENDING)
            {
                /* Here we wait for the start_present_semaphore, once this semaphore is up,
                 * the descendant has finished waiting, we don't want to delete vkImages and vkFences
                 * and semaphores before the waiting is done.
                 */
                res = sem_wait(&desc->m_start_present_semaphore);
                assert(res == 0);
                descendantStartedPresenting = true;
                break;
            }
        }
    }

    /* If descendant started presenting, there is no pending buffer in the swapchain. */
    if (descendantStartedPresenting == false)
    {
        wait_for_pending_buffers();
    }

    /* Make sure the vkFences are done signaling. */
    vkQueueWaitIdle(m_queue);

    /* We are safe to destroy everything. */
    if (m_thread_sem_defined)
    {
        res = pthread_cancel(m_page_flip_thread); //different
        if (res != 0)
        {
            WSI_PRINT_ERROR("pthread_cancel failed for page_flip_thread %lu with %d\n", m_page_flip_thread, res);
        }
        /*
        res = pthread_join(m_page_flip_thread, NULL);
        if (res != 0)
        {
            WSI_PRINT_ERROR("pthread_join failed for page_flip_thread %lu with %d\n", m_page_flip_thread, res);
        }
        */
        res = sem_destroy(&m_page_flip_semaphore);
        if (res != 0)
        {
            WSI_PRINT_ERROR("sem_destroy failed for page_flip_semaphore with %d\n", errno);
        }

        res = sem_destroy(&m_start_present_semaphore);
        if (res != 0)
        {
            WSI_PRINT_ERROR("sem_destroy failed for start_present_semaphore with %d\n", errno);
        }
    }

    if (m_descendant != VK_NULL_HANDLE)
    {
        swapchain *sc = reinterpret_cast<swapchain *>(m_descendant);
        sc->clear_ancestor();
    }

    if (m_ancestor != VK_NULL_HANDLE)
    {
        swapchain *sc = reinterpret_cast<swapchain *>(m_ancestor);
        sc->clear_descendant();
    }

    /* Release the images array. */
    if (m_swapchain_images != nullptr)
    {
        for (uint32_t i = 0; i < m_num_swapchain_images; ++i)
        {
            /* Call implementation specific release */
            release_present_image(m_swapchain_images[i]);
        }

        if (m_alloc_callbacks != nullptr)
        {
            m_alloc_callbacks->pfnFree(m_alloc_callbacks->pUserData, m_swapchain_images);
        }
        else
        {
            free(m_swapchain_images);
        }
    }

    /* Free ring buffer. */
    if (m_pending_buffer_pool.ring != nullptr)
    {
        if (m_alloc_callbacks != nullptr)
        {
            m_alloc_callbacks->pfnFree(m_alloc_callbacks->pUserData, m_pending_buffer_pool.ring);
        }
        else
        {
            free(m_pending_buffer_pool.ring);
        }
    }
}

VkResult swapchain::acquire_next_image(uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex)
{
    VkResult retval = wait_for_free_buffer(timeout);
    if (retval != VK_SUCCESS)
    {
        return retval;
    }

    if (!m_is_valid)
    {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    uint32_t i;
    for (i = 0; i < m_num_swapchain_images; ++i)
    {
        if (m_swapchain_images[i].status == swapchain_image::FREE)
        {
            m_swapchain_images[i].status = swapchain_image::ACQUIRED;
            *pImageIndex = i;
            break;
        }
    }

    assert(i < m_num_swapchain_images);

    if (VK_NULL_HANDLE != semaphore || VK_NULL_HANDLE != fence)
    {
        VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        if (VK_NULL_HANDLE != semaphore)
        {
            submit.signalSemaphoreCount = 1;
            submit.pSignalSemaphores = &semaphore;
        }

        submit.commandBufferCount = 0;
        retval = vkQueueSubmit(m_queue, 1, &submit, fence);
        assert(retval == VK_SUCCESS);
    }
    assert(m_swapchain_images[i].status == swapchain_image::ACQUIRED);
    return retval;
}

VkResult swapchain::get_swapchain_images(uint32_t *pSwapchainImageCount, VkImage *pSwapchainImages)
{
    if (pSwapchainImages == nullptr)
    {
        /* Return the number of swapchain images. */
        *pSwapchainImageCount = m_num_swapchain_images;
        return VK_SUCCESS;
    }
    else
    {
        assert(m_num_swapchain_images > 0);
        assert(*pSwapchainImageCount > 0);

        /* Populate array, write actual number of images returned. */
        uint32_t current_image = 0;
        do
        {
            pSwapchainImages[current_image] = m_swapchain_images[current_image].image;
            current_image++;
            if (current_image == m_num_swapchain_images)
            {
                *pSwapchainImageCount = current_image;
                return VK_SUCCESS;
            }

        } while (current_image < *pSwapchainImageCount);
        /* If pSwapchainImageCount is smaller than the number of presentable images
            * in the swapchain, VK_INCOMPLETE must be returned instead of VK_SUCCESS. */
        *pSwapchainImageCount = current_image;

        return VK_INCOMPLETE;
    }
}

static VkResult mali_vulkan_instrument_frame_end(VkImage image, VkDeviceMemory device_memory, VkQueue queue)
{
    if (VK_NULL_HANDLE == queue) {
        return VK_ERROR_LAYER_NOT_PRESENT;
    }
    return VK_SUCCESS;  //different
}

static VkResult mali_vulkan_instrument_frame_start()   //different
{
    return VK_SUCCESS;
}

VkResult swapchain::queue_present(VkQueue queue, const VkPresentInfoKHR *pPresentInfo, const uint32_t imageIndex)
{
    VkResult result;
    bool descendantStartedPresenting = false;

    if (m_descendant != VK_NULL_HANDLE)
    {
        swapchain *desc = reinterpret_cast<swapchain *>(m_descendant);
        for (uint32_t i = 0; i < desc->m_num_swapchain_images; ++i)
        {
            if (desc->m_swapchain_images[i].status == swapchain_image::PRESENTED ||
                desc->m_swapchain_images[i].status == swapchain_image::PENDING)
            {
                descendantStartedPresenting = true;
                break;
            }
        }
    }

    /* When the semaphore that comes in is signalled, we know that all work is done. So, we do not
     * want to block any future vulkan queue work on it. So, we pass in BOTTOM_OF_PIPE bit as the
     * wait flag.
     */
    VkPipelineStageFlags pipelineStageFlags = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, pPresentInfo->waitSemaphoreCount,
                                pPresentInfo->pWaitSemaphores, &pipelineStageFlags, 0, NULL, 0, NULL };

    assert(m_swapchain_images[imageIndex].status == swapchain_image::ACQUIRED);
    result = vkResetFences(m_device, 1, &m_swapchain_images[imageIndex].present_fence);
    if (result != VK_SUCCESS)
    {
        return result;
    }
    result = vkQueueSubmit(queue, 1, &submitInfo, m_swapchain_images[imageIndex].present_fence);
    if (result != VK_SUCCESS)
    {
        return result;
    }

    /* If the descendant has started presenting, we should release the image
     * however we do not want to block inside the main thread so we mark it
     * as free and let the page flip thread take care of it. */
    if (descendantStartedPresenting)
    {
        m_swapchain_images[imageIndex].status = swapchain_image::FREE;

        m_pending_buffer_pool.ring[m_pending_buffer_pool.tail] = imageIndex;
        m_pending_buffer_pool.tail = (m_pending_buffer_pool.tail + 1) % m_pending_buffer_pool.size;

        sem_post(&m_page_flip_semaphore);

        return VK_ERROR_OUT_OF_DATE_KHR;
    }

    m_swapchain_images[imageIndex].status = swapchain_image::PENDING;
    m_pending_buffer_pool.ring[m_pending_buffer_pool.tail] = imageIndex;
    m_pending_buffer_pool.tail = (m_pending_buffer_pool.tail + 1) % m_pending_buffer_pool.size;
    sem_post(&m_page_flip_semaphore);
    result = instrument_frame_end(m_swapchain_images[imageIndex], queue);

    if (result != VK_SUCCESS)
    {
        WSI_PRINT_ERROR("Frame end instrumentation has failed.\n");
    }

    result = mali_vulkan_instrument_frame_start();
    if (result != VK_SUCCESS)
    {
        WSI_PRINT_ERROR("Frame start instrumentation has failed.\n");
    }

    return VK_SUCCESS;
}

bool swapchain::validate_implied_image_creation_parameters(const VkImageCreateInfo &img_create_info) const
{
    if ((m_create_info.flags & VK_SWAPCHAIN_CREATE_SPLIT_INSTANCE_BIND_REGIONS_BIT_KHR) &&
        !(img_create_info.flags & VK_IMAGE_CREATE_SPLIT_INSTANCE_BIND_REGIONS_BIT_KHR))
    {
        return false;
    }

    if ((m_create_info.flags & VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR) &&
        !(img_create_info.flags & VK_IMAGE_CREATE_PROTECTED_BIT))
    {
        return false;
    }

    if ((m_create_info.flags & VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR) &&
        !((img_create_info.flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) &&
          (img_create_info.flags & VK_IMAGE_CREATE_EXTENDED_USAGE_BIT_KHR)))
    {
        return false;
    }

    if (img_create_info.format != m_create_info.imageFormat)
    {
        return false;
    }

    VkExtent3D image_extent = { m_create_info.imageExtent.width, m_create_info.imageExtent.height, 1 };
    if ((img_create_info.extent.width != image_extent.width) ||
        (img_create_info.extent.height != image_extent.height) || (img_create_info.extent.depth != image_extent.depth))
    {
        return false;
    }

    if (img_create_info.imageType != VK_IMAGE_TYPE_2D)
    {
        return false;
    }

    if (img_create_info.mipLevels != 1)
    {
        return false;
    }

    if (img_create_info.arrayLayers != m_create_info.imageArrayLayers)
    {
        return false;
    }

    if (img_create_info.samples != VK_SAMPLE_COUNT_1_BIT)
    {
        return false;
    }

    if (img_create_info.tiling != VK_IMAGE_TILING_OPTIMAL)
    {
        return false;
    }

    if (img_create_info.usage != m_create_info.imageUsage)
    {
        return false;
    }

    if (img_create_info.sharingMode != m_create_info.imageSharingMode)
    {
        return false;
    }

    if (img_create_info.queueFamilyIndexCount != m_create_info.queueFamilyIndexCount)
    {
        return false;
    }

    for (uint32_t i = 0; i < img_create_info.queueFamilyIndexCount; i++)
    {
        if (img_create_info.pQueueFamilyIndices[i] != m_create_info.pQueueFamilyIndices[i])
        {
            return false;
        }
    }

    if (img_create_info.initialLayout != VK_IMAGE_LAYOUT_UNDEFINED)
    {
        return false;
    }

    return true;
}

void swapchain::deprecate(VkSwapchainKHR descendant)
{
    for (unsigned i = 0; i < m_num_swapchain_images; i++)
    {
        if (m_swapchain_images[i].status == swapchain_image::FREE)
        {
            release_present_image(m_swapchain_images[i]);
        }
    }
    /* Set its descendant. */
    m_descendant = descendant;
}

void swapchain::wait_for_pending_buffers()
{
    int num_acquired_images = 0;
    int wait;

    for (uint32_t i = 0; i < m_num_swapchain_images; ++i)
    {
        if (m_swapchain_images[i].status == swapchain_image::ACQUIRED)
        {
            ++num_acquired_images;
        }
    }

    /* Once all the pending buffers are flipped, the swapchain should have images
     * in ACQUIRED (application fails to queue them back for presentation), FREE
     * and one and only one in PRESENTED. */
    wait = m_num_swapchain_images - num_acquired_images - 1;

    while (wait > 0)
    {
        /* Take down one free image semaphore. */
        wait_for_free_buffer(UINT64_MAX);
        --wait;
    }
}

void swapchain::clear_ancestor()
{
    m_ancestor = VK_NULL_HANDLE;
}

void swapchain::clear_descendant()
{
    m_descendant = VK_NULL_HANDLE;
}

VkResult swapchain::wait_for_free_buffer(uint64_t timeout)
{
    VkResult retval;
    /* first see if a buffer is already marked as free */
    retval = m_free_image_semaphore.wait(0);
    if (retval == VK_NOT_READY)
    {
        /* if not, we still have work to do even if timeout==0 -
         * the swapchain implementation may be able to get a buffer without
         * waiting */

        retval = get_free_buffer(&timeout);
        if (retval == VK_SUCCESS)
        {
            /* the sub-implementation has done it's thing, so re-check the
             * semaphore */
            retval = m_free_image_semaphore.wait(timeout);
        }
    }

    return retval;
}

struct headless_image_data
{
    /* Device memory backing the image. */
    VkDeviceMemory memory;
};

swapchain_headless::swapchain_headless(const VkAllocationCallbacks *pAllocator) : swapchain(pAllocator)
{

}

swapchain_headless::~swapchain_headless()
{
    /* Call the base's teardown */
    teardown();
}

VkResult swapchain_headless::create_swapchain_image(VkDevice device, const VkImageCreateInfo *pCreateInfo,
                                                    const VkAllocationCallbacks *pAllocator, VkImage *pImage)
{
    /* This will be a little weird. What we actually want to happen here is a regular vkCreateImage call.
     * We already hit that and then diverted control to this path due to the pNext structures.
     * To get around this we'll create a shallow copy of the pCreateInfo and call it again, avoiding an
     * infinite recursion. */
    VkImageCreateInfo create_info_copy = *pCreateInfo;
    create_info_copy.pNext = nullptr;

    /* Disable AFBC for swapchain. Needed for vkreplay to work (GRAPHICSSW-15948) */
    create_info_copy.usage |= VK_IMAGE_USAGE_STORAGE_BIT;

    return vkCreateImage(device, &create_info_copy, pAllocator, pImage);
}

VkResult swapchain_headless::bind_swapchain_image(VkDevice device, const VkAllocationCallbacks *pAllocator,
                                                  VkImage image, uint32_t index)
{
    // Find the VkDeviceMemory representing index i.
    headless_image_data *current_image_data =
        reinterpret_cast<headless_image_data *>(m_swapchain_images[index].image_data);
    assert(current_image_data != nullptr);

    return vkBindImageMemory(device, image, current_image_data->memory, 0);
}

VkResult swapchain_headless::instrument_frame_end(swapchain_image &image, VkQueue queue)
{
    headless_image_data *image_data = static_cast<headless_image_data *>(image.image_data);
    return mali_vulkan_instrument_frame_end(image.image, image_data->memory, queue);
}

VkResult swapchain_headless::create_present_image(const VkImageCreateInfo &image_create, swapchain_image &image)
{
    VkResult res = create_swapchain_image(m_device, &image_create, nullptr, &image.image);
    assert(VK_SUCCESS == res);

    VkMemoryRequirements memory_requirements;
    vkGetImageMemoryRequirements(m_device, image.image, &memory_requirements);

    /* Find a memory type */
    size_t mem_type_idx = 0;
    for (; mem_type_idx < 8 * sizeof(memory_requirements.memoryTypeBits); ++mem_type_idx)
    {
        if (memory_requirements.memoryTypeBits & (1u << mem_type_idx))
        {
            break;
        }
    }

    assert(mem_type_idx <= 8 * sizeof(memory_requirements.memoryTypeBits) - 1);

    VkMemoryAllocateInfo mem_info = {};
    mem_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mem_info.allocationSize = memory_requirements.size;
    mem_info.memoryTypeIndex = mem_type_idx;
    headless_image_data *image_data = nullptr;

    /* Create image_data */
    if (m_alloc_callbacks != nullptr)
    {
        image_data = static_cast<headless_image_data *>(m_alloc_callbacks->pfnAllocation(
            m_alloc_callbacks->pUserData, sizeof(headless_image_data), 0, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT));
    }
    else
    {
        image_data = static_cast<headless_image_data *>(malloc(sizeof(headless_image_data)));
    }

    if (image_data == nullptr)
    {
        vkDestroyImage(m_device, image.image, m_alloc_callbacks);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    image.image_data = reinterpret_cast<void *>(image_data);
    image.status = swapchain_image::FREE;

    res = vkAllocateMemory(m_device, &mem_info, nullptr, &image_data->memory);
    assert(VK_SUCCESS == res);
    if (res != VK_SUCCESS)
    {
        release_present_image(image);
        return res;
    }

    res = vkBindImageMemory(m_device, image.image, image_data->memory, 0);
    assert(VK_SUCCESS == res);
    if (res != VK_SUCCESS)
    {
        release_present_image(image);
        return res;
    }

    /* Initialize presentation fence. */
    VkFenceCreateInfo fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0 };
    res = vkCreateFence(m_device, &fenceInfo, nullptr, &image.present_fence);
    if (res != VK_SUCCESS)
    {
        release_present_image(image);
        return res;
    }
    return res;
}

void swapchain_headless::present_image(uint32_t pendingIndex)
{
    unpresent_image(pendingIndex);
}

void swapchain_headless::release_present_image(swapchain_image &image)
{
    if (image.status != swapchain_image::INVALID)
    {
        if (image.present_fence != VK_NULL_HANDLE)
        {
            vkDestroyFence(m_device, image.present_fence, nullptr);
            image.present_fence = VK_NULL_HANDLE;
        }

        if (image.image != VK_NULL_HANDLE)
        {
            vkDestroyImage(m_device, image.image, m_alloc_callbacks);
            image.image = VK_NULL_HANDLE;
        }
    }

    if (image.image_data != nullptr)
    {
        auto *image_data = reinterpret_cast<headless_image_data *>(image.image_data);
        if (image_data->memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(m_device, image_data->memory, nullptr);
            image_data->memory = VK_NULL_HANDLE;
        }
        if (m_alloc_callbacks != nullptr)
        {
            m_alloc_callbacks->pfnFree(m_alloc_callbacks->pUserData, image_data);
        }
        else
        {
            free(image_data);
        }
        image.image_data = nullptr;
    }

    image.status = swapchain_image::INVALID;
}

}