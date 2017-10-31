import { bus } from '../../services/bus.service.js';

Vue.component('sidenav', {
    template: '#sidenav-template',
    methods: {
        openNav() {
            if (window.screen.width <= 480) {
                $('#sideNav').width('60%');
            } else {
                $('#sideNav').width('20%');
            }
        },
        closeNav() {
            $('#sideNav').width('0');
        }
    },
    created() {
        bus.$on('openNav', () => {
            this.openNav();
        });

        bus.$on('closeNav', () => {
            this.closeNav();
        });
    }
});
